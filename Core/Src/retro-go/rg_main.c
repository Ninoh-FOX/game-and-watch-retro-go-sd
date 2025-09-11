#include <odroid_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "appid.h"
#include "main.h"
#include "rg_emulators.h"
#include "gui.h"
#include "gittag.h"
#include "gw_lcd.h"
#include "gw_buttons.h"
#include "gw_flash.h"
#include "gw_sdcard.h"
#include "rg_rtc.h"
#include "rg_i18n.h"
#include "bitmaps.h"
#include "error_screens.h"
#include "gw_malloc.h"
#include "gw_linker.h"
#include "gw_ofw.h"
#if SD_CARD == 1
#include "gw_flash_alloc.h"
#endif

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */

static bool GLOBAL_DATA main_menu_cpu_oc_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (sdcard_hw_type == SDCARD_HW_OSPI1) {
        // Current SD Card design over OSPI1 crash with overclocking,
        // do not allow oc with it until a new flex PCB fix that
        sprintf(option->value, "%s", curr_lang->s_CPU_Overclock_0);
    } else {
        int cpu_oc = odroid_settings_cpu_oc_level_get();
        if (event == ODROID_DIALOG_PREV) {
            if (cpu_oc > 0)
                cpu_oc--;
            else
                cpu_oc = 2;
            SystemClock_Config(cpu_oc);
            odroid_settings_cpu_oc_level_set(cpu_oc);
        }
        else if (event == ODROID_DIALOG_NEXT) {
            if (cpu_oc < 2)
                cpu_oc++;
            else
                cpu_oc = 0;
            SystemClock_Config(cpu_oc);
            odroid_settings_cpu_oc_level_set(cpu_oc);
        }
        switch (cpu_oc) {
        case 1:
            sprintf(option->value, "%s", curr_lang->s_CPU_Overclock_1);
            break;
        case 2:
            sprintf(option->value, "%s", curr_lang->s_CPU_Overclock_2);
            break;
        default:
            sprintf(option->value, "%s", curr_lang->s_CPU_Overclock_0);
            break;
        }
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool GLOBAL_DATA main_menu_timeout_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    const int TIMEOUT_STEP = 60;
    const int TIMEOUT_MIN = 0;
    const int TIMEOUT_MAX = 3600;

    uint16_t timeout = odroid_settings_MainMenuTimeoutS_get();
    if (event == ODROID_DIALOG_PREV)
    {
        timeout = MAX(timeout - TIMEOUT_STEP, TIMEOUT_MIN);
        odroid_settings_MainMenuTimeoutS_set(timeout);
    }
    else if (event == ODROID_DIALOG_NEXT)
    {
        timeout = MIN(timeout + TIMEOUT_STEP, TIMEOUT_MAX);
        odroid_settings_MainMenuTimeoutS_set(timeout);
    }
    sprintf(option->value, "%d %s", odroid_settings_MainMenuTimeoutS_get() / TIMEOUT_STEP, curr_lang->s_Minute);
    return event == ODROID_DIALOG_ENTER;
}


#if COVERFLOW != 0

static bool GLOBAL_DATA theme_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    const char *GW_Themes[] = {curr_lang->s_Theme_sList, curr_lang->s_Theme_CoverV, curr_lang->s_Theme_CoverH, curr_lang->s_Theme_CoverLightH, curr_lang->s_Theme_CoverLightV};
    int8_t theme = odroid_settings_theme_get();

    if (event == ODROID_DIALOG_PREV)
    {
        if (theme > 0)
            odroid_settings_theme_set(--theme);
        else
        {
            theme = 4;
            odroid_settings_theme_set(4);
        }
    }
    else if (event == ODROID_DIALOG_NEXT)
    {
        if (theme < 4)
            odroid_settings_theme_set(++theme);
        else
        {
            theme = 0;
            odroid_settings_theme_set(0);
        }
    }
    sprintf(option->value, "%s", (char *)GW_Themes[theme]);
    return event == ODROID_DIALOG_ENTER;
}
#endif

static bool GLOBAL_DATA colors_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int8_t colors = odroid_settings_colors_get();

    if (event == ODROID_DIALOG_PREV)
    {
        if (colors > 0)
            colors--;
        else
            colors = gui_colors_count - 1;
        odroid_settings_colors_set(colors);
    }
    else if (event == ODROID_DIALOG_NEXT)
    {
        if (colors < gui_colors_count - 1)
            colors++;
        else
            colors = 0;
        odroid_settings_colors_set(colors);
    }
    curr_colors = (colors_t *)(&gui_colors[colors]);
    option->value[0] = 0;
    option->value[10] = 0;
    memcpy(option->value + 2, curr_colors, sizeof(colors_t));
    return event == ODROID_DIALOG_ENTER;
}

static bool GLOBAL_DATA font_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int8_t font = odroid_settings_font_get();

    if (event == ODROID_DIALOG_PREV)
    {
        if (font > 0)
            font--;
        else
            font = gui_font_count - 1;
        odroid_settings_font_set(font);
    }
    else if (event == ODROID_DIALOG_NEXT)
    {
        if (font < gui_font_count - 1)
            font++;
        else
            font = 0;
        odroid_settings_font_set(font);
    }
    set_font(font);
    sprintf(option->value, "%d/%d", font + 1, gui_font_count);
    return event == ODROID_DIALOG_ENTER;
}


static bool GLOBAL_DATA lang_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int8_t lang = odroid_settings_lang_get();

    if (event == ODROID_DIALOG_PREV)
    {
        lang = odroid_settings_get_prior_lang(lang);
        odroid_settings_lang_set(lang);
    }
    else if (event == ODROID_DIALOG_NEXT)
    {
        lang = odroid_settings_get_next_lang(lang);
        odroid_settings_lang_set(lang);
    }
    curr_lang = (lang_t *)gui_lang[lang];
    sprintf(option->value, "%s", curr_lang->s_LangName);
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA hour_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV) {
        GW_AddToCurrentHour(-1);
    }
    else if (event == ODROID_DIALOG_NEXT) {
        GW_AddToCurrentHour(1);
    }

    sprintf(option->value, "%02d", GW_GetCurrentHour());
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA minute_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV) {
        GW_AddToCurrentMinute(-1);
    }
    else if (event == ODROID_DIALOG_NEXT) {
        GW_AddToCurrentMinute(1);
    }

    sprintf(option->value, "%02d", GW_GetCurrentMinute());
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA second_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV) {
        GW_AddToCurrentSecond(-1);
    }
    else if (event == ODROID_DIALOG_NEXT) {
        GW_AddToCurrentSecond(1);
    }

    sprintf(option->value, "%02d", GW_GetCurrentSecond());
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA day_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV) {
        GW_AddToCurrentDay(-1);
    }
    else if (event == ODROID_DIALOG_NEXT) {
        GW_AddToCurrentDay(1);
    }

    sprintf(option->value, "%02d", GW_GetCurrentDay());
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA weekday_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    const char * GW_RTC_Weekday[] = {curr_lang->s_Weekday_Mon, curr_lang->s_Weekday_Tue, curr_lang->s_Weekday_Wed, curr_lang->s_Weekday_Thu, curr_lang->s_Weekday_Fri, curr_lang->s_Weekday_Sat, curr_lang->s_Weekday_Sun};
    sprintf(option->value, "%s", (char *) GW_RTC_Weekday[GW_GetCurrentWeekday() - 1]);
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA month_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV) {
        GW_AddToCurrentMonth(-1);
    }
    else if (event == ODROID_DIALOG_NEXT) {
        GW_AddToCurrentMonth(1);
    }

    sprintf(option->value, "%02d", GW_GetCurrentMonth());
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA year_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV) {
        GW_AddToCurrentYear(-1);
    }
    else if (event == ODROID_DIALOG_NEXT) {
        GW_AddToCurrentYear(1);
    }

    sprintf(option->value, "20%02d", GW_GetCurrentYear());
    return event == ODROID_DIALOG_ENTER;

}

bool GLOBAL_DATA time_display_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    curr_lang->fmtTime(option->value, curr_lang->s_Time_Format, GW_GetCurrentHour(), GW_GetCurrentMinute(), GW_GetCurrentSecond());
    return event == ODROID_DIALOG_ENTER;
}

bool GLOBAL_DATA date_display_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    const char * GW_RTC_Weekday[] = {curr_lang->s_Weekday_Mon, curr_lang->s_Weekday_Tue, curr_lang->s_Weekday_Wed, curr_lang->s_Weekday_Thu, curr_lang->s_Weekday_Fri, curr_lang->s_Weekday_Sat, curr_lang->s_Weekday_Sun};
    curr_lang->fmtDate(option->value, curr_lang->s_Date_Format, GW_GetCurrentDay(), GW_GetCurrentMonth(), GW_GetCurrentYear(), (char *) GW_RTC_Weekday[GW_GetCurrentWeekday() - 1]);
    return event == ODROID_DIALOG_ENTER;
}

static inline bool GLOBAL_DATA tab_enabled(tab_t *tab)
{
    int disabled_tabs = 0;

    if (gui.show_empty)
        return true;

    // If all tabs are disabled then we always return true, otherwise it's an endless loop
    for (int i = 0; i < gui.tabcount; ++i)
        if (gui.tabs[i]->initialized && gui.tabs[i]->is_empty)
            disabled_tabs++;

    return (disabled_tabs == gui.tabcount) || (tab->initialized && !tab->is_empty);
}

#if INTFLASH_BANK == 2
void GLOBAL_DATA soft_reset_do(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x00000000); // Tell patched OFW to stop booting to Retro-Go

    NVIC_SystemReset();
}
#endif

static void GLOBAL_DATA enable_full_debug_clock()
{
    DBGMCU->CR = DBGMCU_CR_DBG_SLEEPCD |
                    DBGMCU_CR_DBG_STOPCD |
                    DBGMCU_CR_DBG_STANDBYCD |
                    DBGMCU_CR_DBG_TRACECKEN |
                    DBGMCU_CR_DBG_CKCDEN |
                    DBGMCU_CR_DBG_CKSRDEN;
}

// This ensures that we are able to catch the cpu while it is waiting for interrupts aka __WFI
static void GLOBAL_DATA enable_minimal_debug_clock()
{
    DBGMCU->CR = DBGMCU_CR_DBG_SLEEPCD;
}

static void GLOBAL_DATA sleep_hook_callback()
{
    if (!odroid_settings_DebugMenuDebugClockAlwaysOn_get())
    {
        // Disable any debug clock
        DBGMCU->CR = 0;
    }
}

static void GLOBAL_DATA update_debug_clock()
{
    odroid_settings_DebugMenuDebugClockAlwaysOn_get() ? enable_full_debug_clock() : enable_minimal_debug_clock();
}

static bool GLOBAL_DATA debug_menu_debug_clock_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    bool always_on = odroid_settings_DebugMenuDebugClockAlwaysOn_get();
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        always_on = !always_on;
        odroid_settings_DebugMenuDebugClockAlwaysOn_set(always_on);
        update_debug_clock();
    }

    sprintf(option->value, "%s", always_on ? curr_lang->s_DBGMCU_clock_on : curr_lang->s_DBGMCU_clock_auto);

    return event == ODROID_DIALOG_ENTER;
}

static bool GLOBAL_DATA debug_menu_debug_cr_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    sprintf(option->value, "0x%08lX", DBGMCU->CR);

    return event == ODROID_DIALOG_ENTER;
}

static void GLOBAL_DATA handle_debug_menu()
{
    uint8_t jedec_id[3];
    char jedec_id_str[16];
    uint8_t status;
    char status_str[8];
    uint8_t config;
    char config_str[8];
    char flash_size_str[16];
    char erase_size_str[32];
    char dbgmcu_id_str[16];
    char dbgmcu_cr_str[16];
    char dbgmcu_clock_str[16];

    // Read jedec id and status register from the external flash
    OSPI_DisableMemoryMappedMode();
    OSPI_ReadJedecId(&jedec_id[0]);
    OSPI_ReadSR(&status);
    OSPI_ReadCR(&config);
    OSPI_EnableMemoryMappedMode();

    snprintf(jedec_id_str, sizeof(jedec_id_str), "%02X %02X %02X", jedec_id[0], jedec_id[1], jedec_id[2]);
    snprintf(status_str, sizeof(status_str), "0x%02X", status);
    snprintf(config_str, sizeof(config_str), "0x%02X", config);
    snprintf(flash_size_str, sizeof(flash_size_str), "%ld MB", OSPI_GetFlashSize() / (1024*1024));
    snprintf(erase_size_str, sizeof(erase_size_str), "%ld kB", OSPI_GetSmallestEraseSize() / 1024);
    snprintf(dbgmcu_id_str, sizeof(dbgmcu_id_str), "0x%08lX", DBGMCU->IDCODE);

    odroid_dialog_choice_t debuginfo[] = {
            {-1, curr_lang->s_Flash_JEDEC_ID, (char *)jedec_id_str, 0, NULL},
            {-1, curr_lang->s_Flash_Name, (char *)OSPI_GetFlashName(), 0, NULL},
            {-1, curr_lang->s_Flash_SR, (char *)status_str, 0, NULL},
            {-1, curr_lang->s_Flash_CR, (char *)config_str, 0, NULL},
            {-1, curr_lang->s_Flash_Size, flash_size_str, 0, NULL},
            {-1, curr_lang->s_Smallest_erase, erase_size_str, 0, NULL},
            ODROID_DIALOG_CHOICE_SEPARATOR,
            {-1, curr_lang->s_DBGMCU_IDCODE, dbgmcu_id_str, 0, NULL},
            {-1, curr_lang->s_DBGMCU_CR, dbgmcu_cr_str, 0, debug_menu_debug_cr_cb},
            {1, curr_lang->s_DBGMCU_clock, dbgmcu_clock_str, 1, debug_menu_debug_clock_cb},
            ODROID_DIALOG_CHOICE_SEPARATOR,
            {0, curr_lang->s_Close, "", 1, NULL},
            ODROID_DIALOG_CHOICE_LAST};

    odroid_overlay_dialog(curr_lang->s_Debug_Title, debuginfo, -1, &gui_redraw_callback);
    odroid_settings_commit();
}

static void GLOBAL_DATA handle_about_menu()
{
    char dialog_title[128];
    odroid_dialog_choice_t choices[] = {
            {-1, curr_lang->s_Author, "ducalex", 0, NULL},
            {-1, curr_lang->s_Author_, "kbeckmann", 0, NULL},
            {-1, curr_lang->s_Author_, "stacksmashing", 0, NULL},
            {-1, curr_lang->s_Author_, "Sylver Bruneau", 0, NULL},
            {-1, curr_lang->s_Author_, "bzhxx", 0, NULL},
            {-1, curr_lang->s_Author_, "Benjamin Sølberg", 0, NULL},
            {-1, curr_lang->s_Author_, "Brian Pugh", 0, NULL},
            {-1, curr_lang->s_UI_Mod, "orzeus", 0, NULL},
            ODROID_DIALOG_CHOICE_SEPARATOR,
            {-1, curr_lang->s_Lang, (char *)curr_lang->s_LangAuthor, 0, NULL},
            ODROID_DIALOG_CHOICE_SEPARATOR,
            {2, curr_lang->s_Debug_menu, "", 1, NULL},
            {1, curr_lang->s_Reset_settings, "", 1, NULL},
            ODROID_DIALOG_CHOICE_SEPARATOR,
            {0, curr_lang->s_Close, "", 1, NULL},
            ODROID_DIALOG_CHOICE_LAST};

    snprintf(dialog_title, sizeof(dialog_title), curr_lang->s_Retro_Go, GIT_TAG);
    int sel = odroid_overlay_dialog(dialog_title, choices, -1, &gui_redraw_callback);
    if (sel == 1)
    {
        // Reset settings
        if (odroid_overlay_confirm(curr_lang->s_Confirm_Reset_settings, false, &gui_redraw_callback) == 1)
        {
            odroid_settings_reset();
            #if SD_CARD == 1
                flash_alloc_reset();
            #endif
            odroid_system_switch_app(0); // reset
        }
    }
    else if (sel == 2)
    {
        handle_debug_menu();
    }
}

static void GLOBAL_DATA handle_options_menu()
{
    char font_value[16];
    char timeout_value[16];
#if COVERFLOW != 0
    char theme_value[16];
#endif
    char colors_value[16];
    char lang_value[64];
    char ov_value[64];

    odroid_dialog_choice_t choices[] = {
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {0, curr_lang->s_Font, font_value, 1, &font_update_cb},
        {0, curr_lang->s_LangUI, lang_value, 1, &lang_update_cb},
#if COVERFLOW != 0
        {0, curr_lang->s_Theme_Title, theme_value, 1, &theme_update_cb},
#endif
        {0x0F0F0E0E, curr_lang->s_Colors, colors_value, 1, &colors_update_cb},
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {0, curr_lang->s_Idle_power_off, timeout_value, 1, &main_menu_timeout_cb},
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {0, curr_lang->s_CPU_Overclock, ov_value, 1, &main_menu_cpu_oc_cb},
#if INTFLASH_BANK == 2
    //{9, curr_lang->s_Reboot, curr_lang->s_Original_system, 1, NULL},
#endif
        ODROID_DIALOG_CHOICE_LAST, // Reserve space to dynamically add more options
        ODROID_DIALOG_CHOICE_LAST};
#if INTFLASH_BANK == 2
    if (get_ofw_extflash_size() != 0) // if OFW is present
    {
        uint8_t ofw_boot_index = sizeof(choices) / sizeof(odroid_dialog_choice_t) - 2;
        choices[ofw_boot_index].id = 9;
        choices[ofw_boot_index].label = curr_lang->s_Reboot;
        choices[ofw_boot_index].value = (char *)curr_lang->s_Original_system;
        choices[ofw_boot_index].enabled = 1;
        choices[ofw_boot_index].update_cb = NULL;
    }
    int r = odroid_overlay_settings_menu(choices, &gui_redraw_callback);
    if (r == 9)
        soft_reset_do();
#else
    odroid_overlay_settings_menu(choices, &gui_redraw_callback);
#endif
}

static void GLOBAL_DATA handle_time_menu()
{
    char time_str[14];
    char date_str[24];

    odroid_dialog_choice_t rtcinfo[] = {
            {0, curr_lang->s_Time, time_str, 1, &time_display_cb},
            {1, curr_lang->s_Date, date_str, 1, &date_display_cb},
            ODROID_DIALOG_CHOICE_LAST};
    int sel = odroid_overlay_dialog(curr_lang->s_Time_Title, rtcinfo, 0, &gui_redraw_callback);

    if (sel == 0)
    {
        static char hour_value[8];
        static char minute_value[8];
        static char second_value[8];

        // Time setup
        odroid_dialog_choice_t timeoptions[8] = {
                {0, curr_lang->s_Hour, hour_value, 1, &hour_update_cb},
                {1, curr_lang->s_Minute, minute_value, 1, &minute_update_cb},
                {2, curr_lang->s_Second, second_value, 1, &second_update_cb},
                ODROID_DIALOG_CHOICE_LAST};
        sel = odroid_overlay_dialog(curr_lang->s_Time_setup, timeoptions, 0, &gui_redraw_callback);
    }
    else if (sel == 1)
    {

        static char day_value[8];
        static char month_value[8];
        static char year_value[8];
        static char weekday_value[8];

        // Date setup
        odroid_dialog_choice_t dateoptions[8] = {
                {2, curr_lang->s_Year, year_value, 1, &year_update_cb},
                {1, curr_lang->s_Month, month_value, 1, &month_update_cb},
                {0, curr_lang->s_Day, day_value, 1, &day_update_cb},
                {-1, curr_lang->s_Weekday, weekday_value, 0, &weekday_update_cb},
                ODROID_DIALOG_CHOICE_LAST};
        sel = odroid_overlay_dialog(curr_lang->s_Date_setup, dateoptions, 0, &gui_redraw_callback);
    }
}

void retro_loop()
{
    tab_t *tab = gui_get_current_tab();
    int last_key = -1;
    int repeat = 0;
    int selected_tab_last = -1;
    uint32_t idle_s;

#pragma GCC diagnostic ignored "-Wint-conversion"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"

    // Read the initial state as to not trigger on button held down during boot
    odroid_input_read_gamepad(&gui.joystick);

    for (int i = 0; i < ODROID_INPUT_MAX; i++)
        if (gui.joystick.values[i])
            last_key = i;

    gui.selected = odroid_settings_MainMenuSelectedTab_get();

    // This will upon power down disable the debug clock to save battery power
    odroid_system_set_sleep_hook(&sleep_hook_callback);

    // This will ensure that we can still catch the CPU during WFI
    update_debug_clock();

    while (true)
    {
        wdog_refresh();

        if (gui.idle_start == 0)
            gui.idle_start = uptime_get();

        idle_s = uptime_get() - gui.idle_start;

        if (gui.selected != selected_tab_last)
        {
            int direction = (gui.selected - selected_tab_last) < 0 ? -1 : 1;

            tab = gui_set_current_tab(gui.selected);
            if (!tab->initialized)
            {
                gui_init_tab(tab);
            } else {
                gui_refresh_tab(tab);
            }

            if (!tab_enabled(tab))
            {
                gui.selected += direction;
                continue;
            }

            selected_tab_last = gui.selected;
        }

        odroid_input_read_gamepad(&gui.joystick);

        if (idle_s > 0 && gui.joystick.bitmask == 0)
        {
            gui_event(TAB_IDLE, tab);
        }

        if ((last_key < 0) || ((repeat >= 30) && (repeat % 5 == 0)))
        {
            for (int i = 0; i < ODROID_INPUT_MAX; i++)
                if (gui.joystick.values[i])
                    last_key = i;

            int key_up = ODROID_INPUT_UP;
            int key_down = ODROID_INPUT_DOWN;
            int key_left = ODROID_INPUT_LEFT;
            int key_right = ODROID_INPUT_RIGHT;
#if COVERFLOW != 0
            int hori_view = odroid_settings_theme_get();
            if ((hori_view== 2) | (hori_view==3))
            {
                key_up = ODROID_INPUT_LEFT;
                key_down = ODROID_INPUT_RIGHT;
                key_left = ODROID_INPUT_UP;
                key_right = ODROID_INPUT_DOWN;
            }
#endif

            if ((last_key == ODROID_INPUT_START) || (last_key == ODROID_INPUT_X))
            {
                handle_about_menu();
            }
            else if ((last_key == ODROID_INPUT_VOLUME) || (last_key == ODROID_INPUT_Y))
            {
                handle_options_menu();
            }
            else if (last_key == ODROID_INPUT_SELECT)
            {
                handle_time_menu();
            }
            else if (last_key == key_up)
            {
                gui_scroll_list(tab, LINE_UP);
                repeat++;
            }
            else if (last_key == key_down)
            {
                gui_scroll_list(tab, LINE_DOWN);
                repeat++;
            }
            else if (last_key == key_left)
            {
                gui.selected--;
                if (gui.selected < 0)
                {
                    gui.selected = gui.tabcount - 1;
                }
                repeat++;
            }
            else if (last_key == key_right)
            {
                gui.selected++;
                if (gui.selected >= gui.tabcount)
                {
                    gui.selected = 0;
                }
                repeat++;
            }
            else if (last_key == ODROID_INPUT_A)
            {
                gui_event(KEY_PRESS_A, tab);
            }
            else if (last_key == ODROID_INPUT_B)
            {
                gui_event(KEY_PRESS_B, tab);
            }
            else if (last_key == ODROID_INPUT_POWER)
            {
                if ((gui.joystick.values[ODROID_INPUT_UP]) || (gui.joystick.values[ODROID_INPUT_DOWN]) ||
                    (gui.joystick.values[ODROID_INPUT_LEFT]) || (gui.joystick.values[ODROID_INPUT_RIGHT]))
                {
                    odroid_system_switch_app(0);
                    return;
                }
                else
                    odroid_system_sleep();
            }
        }
        if (repeat > 0)
            repeat++;
        if (last_key >= 0)
        {
            if (!gui.joystick.values[last_key])
            {
                last_key = -1;
                repeat = 0;
            }
            gui.idle_start = uptime_get();
        }

        idle_s = uptime_get() - gui.idle_start;
        if (odroid_settings_MainMenuTimeoutS_get() != 0 &&
            (idle_s > odroid_settings_MainMenuTimeoutS_get()))
        {
            printf("Idle timeout expired\n");
            odroid_system_sleep();
        }

        // Only redraw if we haven't changed the tab as it has to be initialized first.
        // This will remove an empty frame when changing to a new and uninitialized tab.
        if (gui.selected == selected_tab_last)
        {
            gui_redraw();
        }
    }
}

#define ODROID_APPID_LAUNCHER 0

#if DISABLE_SPLASH_SCREEN == 0
void GLOBAL_DATA app_start_logo()
{
    const int16_t logos[] =   {RG_LOGO_NINTENDO,  RG_LOGO_SEGA,          RG_LOGO_NINTENDO,   RG_LOGO_SEGA,      RG_LOGO_NINTENDO,  RG_LOGO_PCE,        RG_LOGO_SEGA,       RG_LOGO_COLECO,     RG_LOGO_MICROSOFT,  RG_LOGO_WATARA,     RG_LOGO_SEGA,       RG_LOGO_ATARI,        RG_LOGO_AMSTRAD,        RG_LOGO_TAMA};
    const int16_t headers[] = {RG_LOGO_HEADER_GB, RG_LOGO_HEADER_SG1000, RG_LOGO_HEADER_NES, RG_LOGO_HEADER_GG, RG_LOGO_HEADER_GW, RG_LOGO_HEADER_PCE, RG_LOGO_HEADER_SMS, RG_LOGO_HEADER_COL, RG_LOGO_HEADER_MSX, RG_LOGO_HEADER_WSV, RG_LOGO_HEADER_GEN, RG_LOGO_HEADER_A7800, RG_LOGO_HEADER_AMSTRAD, RG_LOGO_HEADER_TAMA};
    retro_logo_image *logo;
    odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, ODROID_SCREEN_HEIGHT, curr_colors->bg_c);
    for (int i = 0; i < 14; i++)
    {
        odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, ODROID_SCREEN_HEIGHT, curr_colors->bg_c);
        logo = rg_get_logo(headers[i]);
        if (logo)
            odroid_overlay_draw_logo((ODROID_SCREEN_WIDTH - logo->width) / 2, 90, headers[i], curr_colors->sel_c);
        logo = rg_get_logo(logos[i]);
        if (logo)
            odroid_overlay_draw_logo((ODROID_SCREEN_WIDTH - logo->width) / 2, 160 + (40 - logo->height) / 2, logos[i], curr_colors->dis_c);
        lcd_sync();
        lcd_swap();
        for (int j = 0; j < 5; j++)
        {
            wdog_refresh();
            HAL_Delay(10);
        }
    }
}
#endif

void GLOBAL_DATA app_logo()
{
    odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, ODROID_SCREEN_HEIGHT, curr_colors->bg_c);
    retro_logo_image *logo;

    for (int i = 1; i <= 10; i++)
    {
        logo = rg_get_logo(RG_LOGO_GNW);
        if (!logo)
            return;
        odroid_overlay_draw_logo((ODROID_SCREEN_WIDTH - logo->width) / 2, 90, RG_LOGO_GNW, 
            get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, i * 10));

        logo = rg_get_logo(RG_LOGO_RGO);
        if (!logo)
            return;
        odroid_overlay_draw_logo((ODROID_SCREEN_WIDTH - logo->width) / 2, 174, RG_LOGO_RGO, 
           get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, i * 10));

        lcd_sync();
        lcd_swap();
        wdog_refresh();
        HAL_Delay(i * 2);
    }
    for (int i = 0; i < 20; i++)
    {
        wdog_refresh();
        HAL_Delay(10);
    }
}

void GLOBAL_DATA app_sleep_logo()
{
    retro_logo_image *logo;
    // As we will use ram_alloc, make sure ram_start pointer is valid
    if (ram_start == 0) {
        ram_start = (uint32_t)&__RAM_EMU_START__;
    }
    for (int i = 10; i <= 100; i+=2)
    {
        logo = rg_get_logo(RG_LOGO_GNW);
        if (!logo)
            return;

        lcd_sleep_while_swap_pending();
        odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, ODROID_SCREEN_HEIGHT, curr_colors->bg_c);
        odroid_overlay_draw_logo((ODROID_SCREEN_WIDTH - logo->width) / 2, 90, RG_LOGO_GNW,
            get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, 110 - i));

        logo = rg_get_logo(RG_LOGO_RGO);
        if (!logo)
            return;

        odroid_overlay_draw_logo((ODROID_SCREEN_WIDTH - logo->width) / 2, 174, RG_LOGO_RGO, 
           get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 110 - i));

        lcd_swap();
        wdog_refresh();
        HAL_Delay(i / 10);
    }
}

void GLOBAL_DATA app_main(uint8_t boot_mode)
{
    lcd_set_buffers(framebuffer1, framebuffer2);
    sdcard_init();
    odroid_system_init(ODROID_APPID_LAUNCHER, 32000);
    odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, ODROID_SCREEN_HEIGHT, curr_colors->bg_c);

    // if OFW is present, write "BOOT" to RTC backup register to always boot to Retro-Go
    // Check game_and_watch_patch project for more details
    if (get_ofw_is_present())
    {
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x544F4F42); // BOOT
    }

    // Init ram start for pseudo dynamic mem allocation
    ahb_init();
    itc_init();
    ram_start = (uint32_t)&__RAM_EMU_START__;

    if (fs_mounted == false) {
        sdcard_error_screen();
    }

    // Re-initialize system now that the filesystem is mounted
    // and apply the correct CPU overclocking level.
    odroid_system_init(ODROID_APPID_LAUNCHER, 32000);
    uint8_t oc = odroid_settings_cpu_oc_level_get();
    SystemClock_Config(oc);

    // Initialize GUI colors based on OFW type
    gui_init_colors();

    emulators_init();

    app_logo();

#if DISABLE_SPLASH_SCREEN == 0
    if (boot_mode != BOOT_MODE_WARM)
        app_start_logo();
#endif

    // Start the previously running emulator directly if it's a valid pointer.
    // If the user holds down the TIME button during startup,start the retro-go
    // gui instead of the last ROM as a fallback.
    char *startup_file = odroid_settings_StartupFile_get();
    retro_emulator_file_t *file = NULL;
    if (strlen(startup_file) > 0) {
        file = emulator_get_file(startup_file);
    }

    if ((file != NULL) && ((GW_GetBootButtons() & B_TIME) == 0)) {
        emulator_start(file, true, true, -1);
    }
    else
    {
        retro_loop();
    }
}
