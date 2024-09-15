#include <assert.h>

#include "odroid_system.h"
#include "rom_manager.h"
#include "gw_linker.h"
#include "gui.h"
#include "main.h"

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

    odroid_settings_init();
    odroid_audio_init(sampleRate);
    odroid_display_init();

    counters.resetTime = get_elapsed_time();

    printf("%s: System ready!\n\n", __func__);
}

void odroid_system_emu_init(state_handler_t load, state_handler_t save, netplay_callback_t netplay_cb)
{
    // currentApp.gameId = crc32_le(0, buffer, sizeof(buffer));
    currentApp.gameId = 0;
    currentApp.loadState = load;
    currentApp.saveState = save;

    printf("%s: Init done. GameId=%08lX\n", __func__, currentApp.gameId);
}

rg_app_desc_t *odroid_system_get_app()
{
    return &currentApp;
}

#if 0
const char OFF_SAVESTATE_PATH[] = "savestate/common";
const char OFF_GNW_DATA_PATH[] = "savestate/common.gnw";
const char OFF_SRAM_PATH[] = "savestate/common.srm";

void odroid_system_get_save_path(char *path, size_t size, int slot)
{
    if (slot == -1)
    {
        strcpy(path, OFF_SAVESTATE_PATH);
    }
    else
    {
        snprintf(path,
                 size,
                 "savestate/%s/%s/%d",
                 ACTIVE_FILE->system->extension,
                 ACTIVE_FILE->name,
                 slot);
    }
}

void odroid_system_get_gnw_data_path(char *path, size_t size, int slot)
{
    if (slot == -1)
    {
        strcpy(path, OFF_GNW_DATA_PATH);
    }
    else
    {
        snprintf(path,
                 size,
                 "savestate/%s/%s/%d.gnw",
                 ACTIVE_FILE->system->extension,
                 ACTIVE_FILE->name,
                 slot);
    }
}

void odroid_system_get_sram_path(char *path, size_t size, int slot)
{
    if (slot == -1)
    {
        strcpy(path, OFF_SRAM_PATH);
    }
    else
    {
        snprintf(path,
                 size,
                 "savestate/%s/%s/%d.srm",
                 ACTIVE_FILE->system->extension,
                 ACTIVE_FILE->name,
                 slot);
    }
}
#endif

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
            strcpy(buffer, ODROID_BASE_PATH_SAVES);
            strcat(buffer, fileName);
            strcat(buffer, ".sav");
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

/* Return true on successful load.
 * Slot -1 is for the OFF_SAVESTATE
 * */
bool odroid_system_emu_load_state(int slot)
{
   return false;
/*
    char savePath[FS_MAX_PATH_SIZE];
    char gnwDataPath[FS_MAX_PATH_SIZE];
    char sramPath[FS_MAX_PATH_SIZE];
    if (currentApp.loadState == NULL)
        return true;
    odroid_system_get_save_path(savePath, sizeof(savePath), slot);
    odroid_system_get_gnw_data_path(gnwDataPath, sizeof(gnwDataPath), slot);
    odroid_system_get_sram_path(sramPath, sizeof(sramPath), slot);
    printf("Attempting to load state from [%s]\n", savePath);
    if (!fs_exists(savePath) && !fs_exists(sramPath))
    {
        printf("Savestate does not exist.\n");
        return false;
    }
    (*currentApp.loadState)(savePath, sramPath, slot);
    if (slot == -1)
    {
        // Delete save files as it's not useful anymore
        if (fs_exists(savePath))
        {
            fs_delete(savePath);
        }
        if (fs_exists(gnwDataPath))
        {
            fs_delete(gnwDataPath);
        }
        if (fs_exists(sramPath))
        {
            fs_delete(sramPath);
        }
    }
    return true;
*/
};

bool odroid_system_emu_save_state(int slot)
{
        return true;
/*
    char savePath[FS_MAX_PATH_SIZE];
    char sramPath[FS_MAX_PATH_SIZE];
    if (currentApp.saveState == NULL)
        return true;

    odroid_system_get_save_path(savePath, sizeof(savePath), slot);
    odroid_system_get_sram_path(sramPath, sizeof(sramPath), slot);
    printf("Savestating to [%s]\n", savePath);
    (*currentApp.saveState)(savePath, sramPath, slot);
    return true;
*/
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
