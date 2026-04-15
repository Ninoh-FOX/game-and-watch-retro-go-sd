#include <stdio.h>
#include <stdbool.h>

#include <stm32h7xx_hal.h>

#include "main.h"
#include "bq24072.h"

#include "utils.h"

#define BQ24072_PROFILING   0

typedef enum {
    BQ24072_PIN_CHG,
    BQ24072_PIN_PGOOD,
    BQ24072_PIN_COUNT       // Keep this last
} bq24072_pin_t;

// PE7 - CHG
// PE8 - CE
// PA2 - PGOOD
// PC4 - Battery voltage

static const struct {
    uint32_t        pin;
    GPIO_TypeDef*   bank;
} bq_pins[BQ24072_PIN_COUNT] = {
    [BQ24072_PIN_CHG]   = { .pin = GPIO_CHARGER_CHARGING_Pin, .bank = GPIO_CHARGER_CHARGING_GPIO_Port},
    [BQ24072_PIN_PGOOD] = { .pin = GPIO_CHARGER_POWERGOOD_Pin, .bank = GPIO_CHARGER_POWERGOOD_GPIO_Port},
};

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;

#if BQ24072_PROFILING
static volatile uint32_t bq24072_battery_value;
#endif // BQ24072_PROFILING

static struct {
    uint16_t    value;
	bool        adc_settle_pending;
    bool        sample_valid;
    uint8_t     sample_count;
    uint32_t    sample_sum;
    bool        charging;
    bool        power_good;
    struct {
        bool            initialized;
        int             percent;
        bq24072_state_t state;
    }           last;
} bq24072_data;

typedef struct {
    uint16_t th[5];
} bq24072_level_table_t;

/*
 * Threshold sets extracted from OFW (FUN_00003148 / FUN_0000320e).
 * OFW selects different tables depending on runtime flags and charge context.
 */
static const bq24072_level_table_t bq24072_levels_low_discharging = {
    .th = {0x29a5, 0x29c5, 0x2cb5, 0x2dcf, 0x2f86},
};

static const bq24072_level_table_t bq24072_levels_low_charging = {
    .th = {0x2a62, 0x2a81, 0x2d71, 0x2e8c, 0x3043},
};

static const bq24072_level_table_t bq24072_levels_high_discharging = {
    .th = {0x8f28, 0x8f94, 0x99af, 0x9d79, 0xa35e},
};

static const bq24072_level_table_t bq24072_levels_high_charging = {
    .th = {0x95e5, 0x9651, 0xa06c, 0xa436, 0xaa1b},
};

static uint8_t bq24072_level_from_table(uint16_t adc_value, const bq24072_level_table_t* table)
{
    if (adc_value > table->th[4]) return 5;
    if (adc_value > table->th[3]) return 4;
    if (adc_value > table->th[2]) return 3;
    if (adc_value > table->th[1]) return 2;
    if (adc_value > table->th[0]) return 1;
    return 0;
}

static int bq24072_percent_from_table(uint16_t adc_value, const bq24072_level_table_t* table)
{
    static const uint8_t soc_anchor[6] = {
        0,   // <= th[0]
        1,   // <= th[1]
        6,   // <= th[2]
        18,  // <= th[3]
        45,  // <= th[4]
        100, // >  th[4]
    };

    int segment;
    uint16_t low_adc;
    uint16_t high_adc;
    int low_percent;
    int high_percent;
    int span_adc;

    // OFW thresholds define level transitions. The first thresholds are very
    // close, so mapping them as 20/40% steps reports too much charge near empty.
    if (adc_value <= table->th[0])
    {
        return soc_anchor[0];
    }

    if (adc_value > table->th[4])
    {
        return soc_anchor[5];
    }

    if (adc_value <= table->th[1])
    {
        segment = 0;
    }
    else if (adc_value <= table->th[2])
    {
        segment = 1;
    }
    else if (adc_value <= table->th[3])
    {
        segment = 2;
    }
    else
    {
        segment = 3;
    }

    low_adc = table->th[segment];
    high_adc = table->th[segment + 1];
    low_percent = soc_anchor[segment + 1];
    high_percent = soc_anchor[segment + 2];

    if (high_adc <= low_adc)
    {
        return low_percent;
    }

    span_adc = (int)high_adc - (int)low_adc;

    return low_percent +
           (((int)adc_value - (int)low_adc) * (high_percent - low_percent)) / span_adc;
}

static const bq24072_level_table_t* bq24072_select_table(uint16_t adc_value, bq24072_state_t state)
{
    bool low_domain = (adc_value < 0x4000);
    bool charging_context = (state == BQ24072_STATE_CHARGING) || (state == BQ24072_STATE_FULL);

    if (low_domain)
    {
        return charging_context ? &bq24072_levels_low_charging : &bq24072_levels_low_discharging;
    }

    return charging_context ? &bq24072_levels_high_charging : &bq24072_levels_high_discharging;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    uint16_t sample;

    if (bq24072_data.adc_settle_pending)
    {
        // Discard the first conversion after each start to avoid stale
       // sample-and-hold residue (especially after warm boots from OFW).
        (void)HAL_ADC_GetValue(hadc);
        bq24072_data.adc_settle_pending = false;
        HAL_ADC_Start_IT(hadc);
        return;
    }

    sample = HAL_ADC_GetValue(hadc);
    bq24072_data.sample_sum += sample;
    bq24072_data.sample_count++;

    // OFW-like behavior: do not trust a single sample. Publish only after
    // accumulating a small window so warm-boot spikes do not leak to UI.
    if (bq24072_data.sample_count >= 8)
    {
        bq24072_data.value = (uint16_t)(bq24072_data.sample_sum / bq24072_data.sample_count);
        bq24072_data.sample_sum = 0;
        bq24072_data.sample_count = 0;
        bq24072_data.sample_valid = true;
    }

#if BQ24072_PROFILING == 1
    bq24072_battery_value = bq24072_data.value;
#endif

    HAL_ADC_Stop_IT(hadc);
}

int32_t bq24072_init(void)
{
    bq24072_data.value = 0;
    bq24072_data.adc_settle_pending = false;
    bq24072_data.sample_valid = false;
    bq24072_data.sample_count = 0;
    bq24072_data.sample_sum = 0;
    bq24072_data.last.initialized = false;

	
    // Read initial states
    bq24072_handle_power_good();
    bq24072_handle_charging();
    bq24072_poll();

    // Start timer for voltage poll
    HAL_TIM_Base_Start_IT(&htim1);

    return 0;
}

void bq24072_interrupts_enable(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /*Configure GPIO pin : GPIO_CHARGER_POWERGOOD_Pin */
    GPIO_InitStruct.Pin = GPIO_CHARGER_POWERGOOD_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIO_CHARGER_POWERGOOD_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin : GPIO_CHARGER_CHARGING_Pin */
    GPIO_InitStruct.Pin = GPIO_CHARGER_CHARGING_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIO_CHARGER_CHARGING_GPIO_Port, &GPIO_InitStruct);
}

void bq24072_interrupts_disable(void)
{
  HAL_GPIO_DeInit(GPIO_CHARGER_POWERGOOD_GPIO_Port, GPIO_CHARGER_POWERGOOD_Pin);  
  HAL_GPIO_DeInit(GPIO_CHARGER_CHARGING_GPIO_Port, GPIO_CHARGER_CHARGING_Pin);  
}

void bq24072_handle_power_good(void)
{
    bq24072_data.power_good = !(HAL_GPIO_ReadPin(bq_pins[BQ24072_PIN_PGOOD].bank, bq_pins[BQ24072_PIN_PGOOD].pin) == GPIO_PIN_SET);
}

void bq24072_handle_charging(void)
{
    bq24072_data.charging = !(HAL_GPIO_ReadPin(bq_pins[BQ24072_PIN_CHG].bank, bq_pins[BQ24072_PIN_CHG].pin) == GPIO_PIN_SET);
}

bq24072_state_t bq24072_get_state(void)
{
    if (bq24072_data.power_good)
    {
        if (bq24072_data.charging)
        {
            return BQ24072_STATE_CHARGING;
        }
        else
        {
            return BQ24072_STATE_FULL;
        }
    }
    else
    {
        if (!bq24072_data.charging)
        {
            return BQ24072_STATE_DISCHARGING;
        }
        else
        {
            return BQ24072_STATE_MISSING;
        }
    }
}

int bq24072_get_percent(void)
{
    bq24072_state_t state;
    const bq24072_level_table_t* table;
    int percent;

    state = bq24072_get_state();
    if (state == BQ24072_STATE_MISSING)
    {
        return 0;
    }

    table = bq24072_select_table(bq24072_data.value, state);
    percent = bq24072_percent_from_table(bq24072_data.value, table);

    // Keep level helper referenced for easier debug correlation with OFW logs.
    (void)bq24072_level_from_table(bq24072_data.value, table);

    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return percent;
}

int bq24072_get_percent_filtered(void)
{
    int             percent;
	int             delta;
    bq24072_state_t state;
	const int       snap_threshold = 5;
    const int       step = 1;

    if (!bq24072_data.sample_valid)
    {
        // No stable averaged value yet: keep previous UI value if available.
        return bq24072_data.last.initialized ? bq24072_data.last.percent : 0;
    }

    state = bq24072_get_state();
    percent = bq24072_get_percent();
	
    if (!bq24072_data.last.initialized)
    {
        bq24072_data.last.initialized = true;
        bq24072_data.last.state = state;
        bq24072_data.last.percent = percent;

        return percent;
    }

    if (state != bq24072_data.last.state)
    {
        bq24072_data.last.state = state;
        bq24072_data.last.percent = percent;

        return percent;
    }

    switch (state)
    {
        case BQ24072_STATE_MISSING:
			bq24072_data.last.percent = 0;
            return 0;
        case BQ24072_STATE_FULL:
			bq24072_data.last.percent = percent;
            return percent;

        case BQ24072_STATE_CHARGING:
        case BQ24072_STATE_DISCHARGING:
            delta = percent - bq24072_data.last.percent;

            if ((state == BQ24072_STATE_DISCHARGING) && (delta > 0))
            {
                // OFW-like discharge behavior: avoid increasing displayed
                // battery level while power is not connected.
                return bq24072_data.last.percent;
            }

            if ((delta >= snap_threshold) || (delta <= -snap_threshold))
            {
                // If the reported value is far from the filtered value,
                // trust it immediately to avoid being pinned to a bad boot sample.
                 bq24072_data.last.percent = percent;
            }


            else if (delta > 0)
            {
                bq24072_data.last.percent += step;
            }
            else if (delta < 0)
            {
                bq24072_data.last.percent -= step;
            }

            return bq24072_data.last.percent;
    }

    return percent;
}

void bq24072_poll(void)
{
	bq24072_data.adc_settle_pending = true;
    HAL_ADC_Start_IT(&hadc1);
}
