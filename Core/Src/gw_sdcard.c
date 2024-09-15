#include "gw_flash.h"
#include "gw_linker.h"
#include "gw_lcd.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "stm32h7xx.h"

#include "main.h"
#include "bitmaps.h"
#include "odroid_colors.h"
#include "odroid_overlay.h"
#include "odroid_input.h"
#include "githash.h"
#include "config.h"
#include "gui.h"
#include "error_screens.h"
#include "ff.h"

bool fs_mounted = false;
static FRESULT cause;

void sdcard_error_screen(void) {
    char buf[64];
    int idle_s = uptime_get();

    switch (cause) {
        case FR_NOT_READY:
            draw_error_screen("No SD CARD found", "Insert SD Card", "Press a key to retry");
            break;
        default:
            draw_error_screen("SD CARD ERROR", "Unable to mount SD Card", "Press a key to retry");
            break;
    }

    while (1)
    {
        odroid_gamepad_state_t joystick;
        wdog_refresh();
        int steps = uptime_get() - idle_s;
        sprintf(buf, "%ds to sleep", 600 - steps);
        odroid_overlay_draw_text_line(4, 29 * 8 - 4, strlen(buf) * 8, buf, C_RED, curr_colors->bg_c);

        lcd_sync();
        lcd_swap();
        //lcd_wait_for_vblank();
        HAL_Delay(10);
        if (steps >= 600)
            break;
        odroid_input_read_gamepad(&joystick);
        if (joystick.values[ODROID_INPUT_POWER] || joystick.values[ODROID_INPUT_A] || joystick.values[ODROID_INPUT_B]){
            break;
        }
    }
    app_sleep_logo();
    GW_EnterDeepSleep();
}

/*************************
 * SD Card Public API *
 *************************/

/**
 * Initialize SD card and mount the filesystem.
 */
FATFS FatFs;  // Fatfs handle
void sdcard_init(void) {
    cause = f_mount(&FatFs, (const TCHAR *)"", 1);
    if (cause == FR_OK) {
        fs_mounted = true;
        printf("filesytem mounted.\n");
    }
}
