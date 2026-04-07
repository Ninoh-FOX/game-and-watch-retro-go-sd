#include "rg_i18n.h"
#include "rg_i18n_lang.h"

extern const lang_t lang_en_us;

lang_t *curr_lang;

void rg_i18n_linux_set_lang(void)
{
    curr_lang = (lang_t *)&lang_en_us;
}

bool odroid_button_turbos(void)
{
    return false;
}

int8_t odroid_settings_turbo_buttons_get(void)
{
    return 0;
}

void odroid_settings_turbo_buttons_set(int8_t turbo_buttons)
{
    (void)turbo_buttons;
}
