#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "rg_i18n.h"
#include "rg_rtc.h"
#include "gui.h"

#define WELCOME_PROMPT_NONE   0u
#define WELCOME_PROMPT_SHOWN  1u
#define WELCOME_MIN_FULL_YEAR 2026u
#define WELCOME_DELAY_SEC     ((time_t)(5L * 24 * 60 * 60))

static const char WELCOME_MSG[] =
    "Join https://www.patreon.com/sylverb  to get latest G&W Retro-Go news";

static bool welcome_rtc_year_valid(void)
{
    return (2000u + (uint32_t)GW_GetCurrentYear()) >= WELCOME_MIN_FULL_YEAR;
}

static uint32_t welcome_encode_current_ymd(void)
{
    uint32_t y = 2000u + (uint32_t)GW_GetCurrentYear();
    uint32_t m = (uint32_t)GW_GetCurrentMonth();
    uint32_t d = (uint32_t)GW_GetCurrentDay();
    return y * 10000u + m * 100u + d;
}

static time_t welcome_ymd_to_time(uint32_t yyyymmdd)
{
    struct tm tmv;

    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = (int)(yyyymmdd / 10000u) - 1900;
    tmv.tm_mon = (int)((yyyymmdd / 100u) % 100u) - 1;
    tmv.tm_mday = (int)(yyyymmdd % 100u);
    tmv.tm_hour = 12;
    return mktime(&tmv);
}

void rg_welcome_prompt_show_dialog(void)
{
    odroid_dialog_choice_t choices[] = {
        {-1, WELCOME_MSG, "", -1, NULL},
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {0, curr_lang->s_OK, "", 1, NULL},
        ODROID_DIALOG_CHOICE_LAST,
    };

    odroid_overlay_dialog(curr_lang->s_Patreon_menu, choices, 2, &gui_redraw_callback, 0);
}

void rg_welcome_prompt_maybe_auto_show_on_launcher(void)
{
    uint32_t v = odroid_settings_WelcomePrompt_get();

    if (v == WELCOME_PROMPT_SHOWN)
        return;

    if (v == WELCOME_PROMPT_NONE) {
        if (welcome_rtc_year_valid()) {
            uint32_t ymd = welcome_encode_current_ymd();
            if (ymd > WELCOME_PROMPT_SHOWN) {
                odroid_settings_WelcomePrompt_set(ymd);
                odroid_settings_commit();
            }
        }
        return;
    }

    time_t t0 = welcome_ymd_to_time(v);
    if (t0 == (time_t)-1) {
        odroid_settings_WelcomePrompt_set(WELCOME_PROMPT_NONE);
        odroid_settings_commit();
        return;
    }

    time_t now = GW_GetUnixTime();
    if (now != (time_t)-1 && now >= t0 + WELCOME_DELAY_SEC) {
        rg_welcome_prompt_show_dialog();
        odroid_settings_WelcomePrompt_set(WELCOME_PROMPT_SHOWN);
        odroid_settings_commit();
    }
}
