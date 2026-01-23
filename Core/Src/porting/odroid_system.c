#include <assert.h>

#include "odroid_system.h"
#include "rom_manager.h"
#include "gw_linker.h"
#include "rg_rtc.h"
#include "gui.h"
#include "main.h"
#include "gw_lcd.h"
#include "gw_sleep.h"
#if SD_CARD == 1
#include "gw_sdcard.h"
#include "ff.h"
#endif

static rg_app_desc_t currentApp;
static runtime_stats_t statistics;
static runtime_counters_t counters;
static uint skip;

static sleep_pre_sleep_hook_t pre_sleep_hook = NULL;

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

void odroid_system_emu_init(state_handler_t load_cb,
                            state_handler_t save_cb,
                            screenshot_handler_t screenshot_cb,
                            shutdown_handler_t shutdown_cb,
                            sleep_post_wakeup_handler_t sleep_post_wakeup_cb)
{
    // currentApp.gameId = crc32_le(0, buffer, sizeof(buffer));
    currentApp.gameId = 0;
    currentApp.handlers.loadState = load_cb;
    currentApp.handlers.saveState = save_cb;
    currentApp.handlers.screenshot = screenshot_cb;
    currentApp.handlers.shutdown = shutdown_cb;
    currentApp.handlers.sleep_post_wakeup = sleep_post_wakeup_cb;

    printf("%s: Init done. GameId=%08lX\n", __func__, currentApp.gameId);
}

rg_app_desc_t *odroid_system_get_app()
{
    return &currentApp;
}

static char *extract_system(const char *filename) {
    char *last_slash = strrchr(filename, '/');
    char *directory = malloc(last_slash - filename + 2);
    strncpy(directory, filename, last_slash - filename + 1);
    directory[last_slash - filename + 1] = '\0';
    return directory;
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
        case ODROID_PATH_SAVE_STATE_OFF:
            sprintf(buffer, "%s/off.sav", ODROID_BASE_PATH_SAVES);
            break;
        case ODROID_PATH_SCREENSHOT:
        case ODROID_PATH_SCREENSHOT_1:
        case ODROID_PATH_SCREENSHOT_2:
        case ODROID_PATH_SCREENSHOT_3:
            sprintf(buffer, "%s%s-%d.raw", ODROID_BASE_PATH_SAVES, fileName, type-ODROID_PATH_SCREENSHOT);
            break;

        case ODROID_PATH_USER_SCREENSHOT:
        {
            // Get current date and time using standard C functions
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            
            // Extract just the filename without path and extension
            char tempFileName[200];
            const char *baseName = strrchr(fileName, '/');
            if (baseName) {
                baseName++; // Skip the '/'
            } else {
                baseName = fileName;
            }
            strncpy(tempFileName, baseName, sizeof(tempFileName) - 1);
            char *dot = strrchr(tempFileName, '.');
            if (dot) *dot = '\0';
            
            sprintf(buffer, "%s/%04d-%02d-%02d-%02d-%02d-%02d-%s.bmp", 
                    ODROID_BASE_PATH_SCREENSHOTS, 
                    1900 + tm_info->tm_year, tm_info->tm_mon + 1, tm_info->tm_mday,
                    tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tempFileName);
            break;
        }

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

        case ODROID_PATH_COVER_FILE:
        {
            char tempFileName[200];
            strncpy(tempFileName, fileName, sizeof(tempFileName) - 1);
            char *dot = strrchr(tempFileName, '.');
            if (dot)
            {
               *dot = '\0';
            }
            sprintf(buffer, "%s%s.img", ODROID_BASE_PATH_COVERS, tempFileName);
            break;
        }

        case ODROID_PATH_CHEAT_STATE:
        {
            char *shortFileName = strdup(fileName);
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            sprintf(buffer, "%s%s.state", ODROID_BASE_PATH_CHEATS, fileName);
            free(shortFileName);
            break;
        }

        case ODROID_PATH_CHEAT_PCE:
        {
            char *shortFileName = strdup(fileName);
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            sprintf(buffer, "%s%s.pceplus", ODROID_BASE_PATH_CHEATS, shortFileName);
            free(shortFileName);
            break;
        }

        case ODROID_PATH_CHEAT_GAME_GENIE:
        {
            char *shortFileName = strdup(fileName);
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            sprintf(buffer, "%s%s.ggcodes", ODROID_BASE_PATH_CHEATS, shortFileName);
            free(shortFileName);
            break;
        }

        case ODROID_PATH_CHEAT_MCF:
        {
            char *shortFileName = strdup(fileName);
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            sprintf(buffer, "%s%s.mcf", ODROID_BASE_PATH_CHEATS, shortFileName);
            free(shortFileName);
            break;
        }

        case ODROID_PATH_SYSTEM_CONFIG:
        {
            char *systemPath = extract_system(fileName);
            sprintf(buffer, "%s%sCONFIG", ODROID_BASE_PATH_CONFIG, systemPath);
            free(systemPath);
            break;
        }

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
    if (slot == -2) {
        filename = NULL;
    } else if (slot == -1) {
        filename = odroid_system_get_path(ODROID_PATH_SAVE_STATE_OFF, currentApp.romPath);
    } else {
        filename = odroid_system_get_path(ODROID_PATH_SAVE_STATE + slot, currentApp.romPath);
    }
    bool success = false;

    if (filename) {
        printf("Loading state from '%s'.\n", filename);
    } else {
        printf("Loading state from sram.\n");
    }

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
        filename = odroid_system_get_path(ODROID_PATH_SAVE_STATE_OFF, currentApp.romPath);
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

void odroid_system_shutdown() {
    if (currentApp.handlers.shutdown) {
        (*currentApp.handlers.shutdown)();
    }
}

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

#if SD_CARD == 1
        // Unmount Fs and Deinit SD Card if needed
        sdcard_deinit();
#endif

#if 1
        // Jumping directly to bank2 entrypoint instead of rebooting is much faster

        void __attribute__((naked)) _start_app(void (*const pc)(void), uint32_t sp) {
            __asm("           \n\
                  msr msp, r1 /* load r1 into MSP */\n\
                  bx r0       /* branch to the address at r0 */\n\
            ");
        }

        void _boot_bank2(void) {
            uint32_t sp = *((uint32_t *)FLASH_BANK2_BASE);
            uint32_t pc = *((uint32_t *)FLASH_BANK2_BASE + 1);

            // Check that Bank 2 content is valid
            __set_MSP(sp);
            __set_PSP(sp);
            HAL_MPU_Disable();
            _start_app((void (*const)(void))pc, (uint32_t)sp);
        }

        boot_magic_set(BOOT_MAGIC_EMULATOR);

        app_animate_lcd_brightness(odroid_display_get_backlight_raw(), 0, 10);

        HAL_DeInit();

        SCB_InvalidateDCache();
        SCB_InvalidateICache();

        while (1) {
            _boot_bank2();
        }
#else
        *((uint32_t *)0x2001FFF8) = 0x544F4F42;              // "BOOT"
        *((uint32_t *)0x2001FFFC) = (uint32_t)&__INTFLASH__; // vector table

        NVIC_SystemReset();
#endif
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

runtime_stats_t odroid_system_get_stats(bool reset_stats)
{
    float tickTime = (get_elapsed_time() - counters.resetTime);

    statistics.battery = odroid_input_read_battery();
    statistics.busyPercent = counters.busyTime / tickTime * 100.f;
    statistics.skippedFPS = counters.skippedFrames / (tickTime / 1000.f);
    statistics.totalFPS = counters.totalFrames / (tickTime / 1000.f);

    if (reset_stats) {
        skip = 1;
        counters.busyTime = 0;
        counters.totalFrames = 0;
        counters.skippedFrames = 0;
        counters.resetTime = get_elapsed_time();
    }

    return statistics;
}

void odroid_system_set_pre_sleep_hook(sleep_pre_sleep_hook_t callback)
{
    pre_sleep_hook = callback;
}

static void odroid_system_sleep_post_wakeup_handler() {
    if (currentApp.handlers.sleep_post_wakeup) {
        (*currentApp.handlers.sleep_post_wakeup)();
    }
}

static void odroid_system_sleep_internal(system_sleep_flags_t flags, sleep_pre_wakeup_callback_t pre_wakeup_callback)
{
    if (pre_sleep_hook != NULL)
    {
        pre_sleep_hook();
    }
    odroid_settings_StartupFile_set(ACTIVE_FILE);
    odroid_settings_commit();

    if (flags & SLEEP_SHOW_ANIMATION) {
        app_sleep_transition((flags & SLEEP_SHOW_LOGO) != 0, (flags & SLEEP_ANIMATION_SLOW) != 0);
    }

    gui_save_current_tab();

    if (flags & SLEEP_ENTER_SLEEP) {
        GW_EnterDeepSleep(false, pre_wakeup_callback, &odroid_system_sleep_post_wakeup_handler);
    } else if (flags & SLEEP_ENTER_STANDBY) {
        GW_EnterDeepSleep(true, NULL, NULL);
    }
}

void odroid_system_sleep_ex(system_sleep_flags_t flags, sleep_pre_wakeup_callback_t pre_wakeup_callback)
{
    odroid_system_sleep_internal(flags, pre_wakeup_callback);
}

void odroid_system_sleep(void)
{
    odroid_system_sleep_internal(SLEEP_ENTER_SLEEP_WITH_ANIMATION, NULL);
}
