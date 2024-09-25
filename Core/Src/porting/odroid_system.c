#include <assert.h>

#include "odroid_system.h"
#include "rom_manager.h"
#include "gw_linker.h"
#include "gui.h"
#include "main.h"
#include "gw_lcd.h"

static rg_app_desc_t currentApp;
static runtime_stats_t statistics;
static runtime_counters_t counters;
static uint skip;

static sleep_hook_t sleep_hook = NULL;

#define TURBOS_SPEED 10

bool odroid_button_turbos(void)
{
    int turbos = 1000 / TURBOS_SPEED;
    return (get_elapsed_time() % turbos) < (turbos / 2);
}

void odroid_system_panic(const char *reason, const char *function, const char *file)
{
    printf("*** PANIC: %s\n  *** FUNCTION: %s\n  *** FILE: %s\n", reason, function, file);
    assert(0);
}

void odroid_system_init(int appId, int sampleRate)
{
    currentApp.id = appId;
    currentApp.romPath = ACTIVE_FILE->path;

    odroid_settings_init();
    odroid_audio_init(sampleRate);
    odroid_display_init();

    counters.resetTime = get_elapsed_time();

    printf("%s: System ready!\n\n", __func__);
}

void odroid_system_emu_init(state_handler_t load, state_handler_t save, screenshot_handler_t screenshot_cb)
{
    // currentApp.gameId = crc32_le(0, buffer, sizeof(buffer));
    currentApp.gameId = 0;
    currentApp.handlers.loadState = load;
    currentApp.handlers.saveState = save;
    currentApp.handlers.screenshot = screenshot_cb;

    printf("%s: Init done. GameId=%08lX\n", __func__, currentApp.gameId);
}

rg_app_desc_t *odroid_system_get_app()
{
    return &currentApp;
}


char* odroid_system_get_path(emu_path_type_t type, const char *_romPath)
{
    const char *fileName = _romPath ?: currentApp.romPath;
    char buffer[256];

    if (strstr(fileName, ODROID_BASE_PATH_ROMS))
    {
        fileName = strstr(fileName, ODROID_BASE_PATH_ROMS);
        fileName += strlen(ODROID_BASE_PATH_ROMS);
    }

    if (!fileName || strlen(fileName) < 4)
    {
        RG_PANIC("Invalid ROM path!");
    }

    switch (type)
    {
        case ODROID_PATH_SAVE_STATE:
        case ODROID_PATH_SAVE_STATE_1:
        case ODROID_PATH_SAVE_STATE_2:
        case ODROID_PATH_SAVE_STATE_3:
            sprintf(buffer, "%s%s-%d.sav", ODROID_BASE_PATH_SAVES, fileName, type);
            break;

        case ODROID_PATH_SCREENSHOT:
        case ODROID_PATH_SCREENSHOT_1:
        case ODROID_PATH_SCREENSHOT_2:
        case ODROID_PATH_SCREENSHOT_3:
            sprintf(buffer, "%s%s-%d.raw", ODROID_BASE_PATH_SAVES, fileName, type-ODROID_PATH_SCREENSHOT);
            break;

        case ODROID_PATH_SAVE_BACK:
            strcpy(buffer, ODROID_BASE_PATH_SAVES);
            strcat(buffer, fileName);
            strcat(buffer, ".sav.bak");
            break;

        case ODROID_PATH_SAVE_SRAM:
            strcpy(buffer, ODROID_BASE_PATH_SAVES);
            strcat(buffer, fileName);
            strcat(buffer, ".sram");
            break;

        case ODROID_PATH_TEMP_FILE:
            sprintf(buffer, "%s/%X%X.tmp", ODROID_BASE_PATH_TEMP, get_elapsed_time(), rand());
            break;

        case ODROID_PATH_ROM_FILE:
            strcpy(buffer, ODROID_BASE_PATH_ROMS);
            strcat(buffer, fileName);
            break;

        case ODROID_PATH_CRC_CACHE:
            strcpy(buffer, ODROID_BASE_PATH_CRC_CACHE);
            strcat(buffer, fileName);
            strcat(buffer, ".crc");
            break;

        default:
            RG_PANIC("Unknown Type");
    }

    return strdup(buffer);
}

bool odroid_system_emu_screenshot(const char *filename)
{
    bool success = false;

    rg_storage_mkdir(rg_dirname(filename));

    uint8_t *data;
    size_t size = sizeof(framebuffer1);
    if (currentApp.handlers.screenshot) {
        data = (*currentApp.handlers.screenshot)();
    } else {
        // If there is no callback for screenshot, we take it from framebuffer
        // which is not the best as it will include menu in the middle
        lcd_wait_for_vblank();
        data = (unsigned char *)lcd_get_inactive_buffer();
    }

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(data, 1, size, file);

    fclose(file);
    
    if (written != size) {
        return false;
    }
    success = true;

    rg_storage_commit();

    return success;
}

rg_emu_states_t *odroid_system_emu_get_states(const char *romPath, size_t slots)
{
    rg_emu_states_t *result = (rg_emu_states_t *)calloc(1, sizeof(rg_emu_states_t) + sizeof(rg_emu_slot_t) * slots);
    uint8_t last_used_slot = 0xFF;

    char *filename = odroid_system_get_path(ODROID_PATH_SAVE_STATE, romPath);
    FILE *fp = fopen(filename, "rb");
    if (fp)
    {
        fread(&last_used_slot, 1, 1, fp);
        fclose(fp);
    }
    free(filename);

    for (size_t i = 0; i < slots; i++)
    {
        rg_emu_slot_t *slot = &result->slots[i];
        char *preview = odroid_system_get_path(ODROID_PATH_SCREENSHOT + i, romPath);
        char *file = odroid_system_get_path(ODROID_PATH_SAVE_STATE + i, romPath);
        rg_stat_t info = rg_storage_stat(file);
        strcpy(slot->preview, preview);
        strcpy(slot->file, file);
        slot->id = i;
        slot->is_used = info.exists;
        slot->is_lastused = false;
        slot->mtime = info.mtime;
        if (slot->is_used)
        {
            if (!result->latest || slot->mtime > result->latest->mtime)
                result->latest = slot;
            if (slot->id == last_used_slot)
                result->lastused = slot;
            result->used++;
        }
        free(preview);
        free(file);
    }
    if (!result->lastused && result->latest)
        result->lastused = result->latest;
    if (result->lastused)
        result->lastused->is_lastused = true;
    result->total = slots;

    return result;
}

/* Return true on successful load.
 * Slot -1 is for the OFF_SAVESTATE
 * */
bool odroid_system_emu_load_state(int slot)
{
    if (!currentApp.romPath || !currentApp.handlers.loadState)
    {
        printf("No rom or handler defined...\n");
        return false;
    }

    char *filename;
    if (slot == -1) {
        filename = ODROID_BASE_PATH_SAVES "/off.sav";
    } else {
        filename = odroid_system_get_path(ODROID_PATH_SAVE_STATE + slot, currentApp.romPath);
    }
    bool success = false;

    printf("Loading state from '%s'.\n", filename);

    success = (*currentApp.handlers.loadState)(filename);

    free(filename);

    return success;
};

bool odroid_system_emu_save_state(int slot)
{
    if (!currentApp.romPath || !currentApp.handlers.saveState)
    {
        printf("No rom or handler defined...\n");
        return false;
    }

    char *filename;
    if (slot == -1) {
        filename = ODROID_BASE_PATH_SAVES "/off.sav";
    } else {
        filename = odroid_system_get_path(ODROID_PATH_SAVE_STATE + slot, currentApp.romPath);
    }

    bool success = false;

    printf("Saving state to '%s'.\n", filename);

    if (!rg_storage_mkdir(rg_dirname(filename)))
    {
        printf("Unable to create dir, save might fail...\n");
    }

    success = (*currentApp.handlers.saveState)(filename);

    if ((success) && (slot >= 0))
    {
        // Save succeeded, let's take a pretty screenshot for the launcher!
        char *filename = odroid_system_get_path(ODROID_PATH_SCREENSHOT + slot, currentApp.romPath);
        odroid_system_emu_screenshot(filename);
        free(filename);
    }

    free(filename);

    rg_storage_commit();

    return success;
};

IRAM_ATTR void odroid_system_tick(uint skippedFrame, uint fullFrame, uint busyTime)
{
    if (skippedFrame)
        counters.skippedFrames++;
    else if (fullFrame)
        counters.fullFrames++;
    counters.totalFrames++;

    // Because the emulator may have a different time perception, let's just skip the first report.
    if (skip)
    {
        skip = 0;
    }
    else
    {
        counters.busyTime += busyTime;
    }

    statistics.lastTickTime = get_elapsed_time();
}

void odroid_system_switch_app(int app)
{
    printf("%s: Switching to app %d.\n", __FUNCTION__, app);

    switch (app)
    {
    case 0:
        odroid_settings_StartupFile_set(0);
        odroid_settings_commit();

        /**
         * Setting these two places in memory tell tim's patched firmware
         * bootloader running in bank 1 (0x08000000) to boot into retro-go
         * immediately instead of the patched-stock-firmware..
         *
         * These are the last 8 bytes of the 128KB of DTCM RAM.
         *
         * This uses a technique described here:
         *      https://stackoverflow.com/a/56439572
         *
         *
         * For stuff not running a bootloader like this, these commands are
         * harmless.
         */
        *((uint32_t *)0x2001FFF8) = 0x544F4F42;              // "BOOT"
        *((uint32_t *)0x2001FFFC) = (uint32_t)&__INTFLASH__; // vector table

        NVIC_SystemReset();
        break;
    case 9:
        *((uint32_t *)0x2001FFF8) = 0x544F4F42;              // "BOOT"
        *((uint32_t *)0x2001FFFC) = (uint32_t)&__INTFLASH__; // vector table

        NVIC_SystemReset();
        break;
    default:
        assert(0);
    }
}

runtime_stats_t odroid_system_get_stats()
{
    float tickTime = (get_elapsed_time() - counters.resetTime);

    statistics.battery = odroid_input_read_battery();
    statistics.busyPercent = counters.busyTime / tickTime * 100.f;
    statistics.skippedFPS = counters.skippedFrames / (tickTime / 1000.f);
    statistics.totalFPS = counters.totalFrames / (tickTime / 1000.f);

    skip = 1;
    counters.busyTime = 0;
    counters.totalFrames = 0;
    counters.skippedFrames = 0;
    counters.resetTime = get_elapsed_time();

    return statistics;
}

void odroid_system_set_sleep_hook(sleep_hook_t callback)
{
    sleep_hook = callback;
}

void odroid_system_sleep(void)
{
    if (sleep_hook != NULL)
    {
        sleep_hook();
    }
//    odroid_settings_StartupFile_set(ACTIVE_FILE);

    // odroid_settings_commit();
    gui_save_current_tab();
    app_sleep_logo();

    GW_EnterDeepSleep();
}
