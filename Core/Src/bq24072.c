#include <stdio.h>
#include <stdbool.h>

#include <stm32h7xx_hal.h>

#include "main.h"
#include "bq24072.h"

#include "utils.h"

#define BQ24072_BATTERY_FULL    13000
#define BQ24072_BATTERY_LOWBAT  11000

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
    int     span = BQ24072_BATTERY_FULL - BQ24072_BATTERY_LOWBAT;

    if (bq24072_get_state() == BQ24072_STATE_MISSING)
    {
        return 0;
    }

    if (bq24072_data.value - BQ24072_BATTERY_LOWBAT <= 0)
    {
        return 0;
    }
    else if (bq24072_data.value >= BQ24072_BATTERY_FULL)
    {
        return 100;
    }
    else
    {
        return (bq24072_data.value - BQ24072_BATTERY_LOWBAT)*100 / span;
    }
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
