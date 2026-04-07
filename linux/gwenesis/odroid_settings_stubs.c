#include "odroid_settings.h"

static char rom_path_buf[4] = "";

void odroid_settings_init(void) {}

void odroid_settings_reset(void) {}

void odroid_settings_commit(void) {}

void odroid_settings_FontSize_set(int32_t v) { (void)v; }
void odroid_settings_Volume_set(int32_t v) { (void)v; }
void odroid_settings_RomFilePath_set(const char *v) { (void)v; }
void odroid_settings_Backlight_set(int32_t v) { (void)v; }
void odroid_settings_StartupApp_set(int32_t v) { (void)v; }
void odroid_settings_StartupFile_set(retro_emulator_file_t *v) { (void)v; }
void odroid_settings_MainMenuTimeoutS_set(uint16_t v) { (void)v; }
void odroid_settings_MainMenuSelectedTab_set(uint16_t v) { (void)v; }
void odroid_settings_MainMenuCursor_set(uint16_t v) { (void)v; }
void odroid_settings_StartAction_set(ODROID_START_ACTION v) { (void)v; }
void odroid_settings_AudioSink_set(int32_t v) { (void)v; }
void odroid_settings_Region_set(ODROID_REGION v) { (void)v; }
void odroid_settings_Palette_set(int32_t v) { (void)v; }
void odroid_settings_SpriteLimit_set(int32_t v) { (void)v; }
void odroid_settings_DisplayScaling_set(int32_t v) { (void)v; }
void odroid_settings_DisplayFilter_set(int32_t v) { (void)v; }
void odroid_settings_DisplayRotation_set(int32_t v) { (void)v; }
void odroid_settings_DisplayOverscan_set(int32_t v) { (void)v; }

bool odroid_settings_ActiveGameGenieCodes_is_enabled(char *game_path, int code_index)
{
    (void)game_path;
    (void)code_index;
    return false;
}

bool odroid_settings_ActiveGameGenieCodes_set(char *game_path, int code_index, bool enable)
{
    (void)game_path;
    (void)code_index;
    (void)enable;
    return false;
}

bool odroid_settings_DebugMenuDebugClockAlwaysOn_get(void) { return false; }
void odroid_settings_DebugMenuDebugClockAlwaysOn_set(bool v) { (void)v; }

void odroid_settings_string_set(const char *key, const char *value)
{
    (void)key;
    (void)value;
}

void odroid_settings_int32_set(const char *key, int32_t value)
{
    (void)key;
    (void)value;
}

void odroid_settings_app_int32_set(const char *key, int32_t value)
{
    (void)key;
    (void)value;
}

void odroid_settings_cpu_oc_level_set(uint8_t oc) { (void)oc; }

int32_t odroid_settings_FontSize_get(void) { return 0; }
int32_t odroid_settings_Volume_get(void) { return 10; }
char *odroid_settings_RomFilePath_get(void) { return rom_path_buf; }
int32_t odroid_settings_Backlight_get(void) { return 255; }
int32_t odroid_settings_StartupApp_get(void) { return 0; }
char *odroid_settings_StartupFile_get(void) { return NULL; }
uint16_t odroid_settings_MainMenuTimeoutS_get(void) { return 0; }
uint16_t odroid_settings_MainMenuSelectedTab_get(void) { return 0; }
uint16_t odroid_settings_MainMenuCursor_get(void) { return 0; }
ODROID_START_ACTION odroid_settings_StartAction_get(void) { return ODROID_START_ACTION_RESUME; }
int32_t odroid_settings_AudioSink_get(void) { return 0; }
ODROID_REGION odroid_settings_Region_get(void) { return ODROID_REGION_AUTO; }
int32_t odroid_settings_Palette_get(void) { return 0; }
int32_t odroid_settings_SpriteLimit_get(void) { return 0; }
int32_t odroid_settings_DisplayScaling_get(void) { return 0; }
int32_t odroid_settings_DisplayFilter_get(void) { return 0; }
int32_t odroid_settings_DisplayRotation_get(void) { return 0; }
int32_t odroid_settings_DisplayOverscan_get(void) { return 0; }

char *odroid_settings_string_get(const char *key, const char *default_value)
{
    (void)key;
    return (char *)default_value;
}

int32_t odroid_settings_int32_get(const char *key, int32_t value_default)
{
    (void)key;
    return value_default;
}

int32_t odroid_settings_app_int32_get(const char *key, int32_t value_default)
{
    (void)key;
    return value_default;
}

uint8_t odroid_settings_cpu_oc_level_get(void) { return 0; }
