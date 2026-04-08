/*
 * PICO-8 engine integration for Game & Watch Retro-Go
 *
 * Uses the embedded frame-stepping API from p8_core.h.
 * Cart data comes from flash cache (SD → external flash → XIP pointer).
 */

extern "C" {
#include <odroid_system.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "main_pico8.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "gw_audio.h"
#include "gw_malloc.h"
#include "common.h"
#include "rom_manager.h"
#include "appid.h"
}

/* PICO-8 engine headers */
#include "p8_core.h"
#include "p8_pool_alloc.h"
#include "sys_api.h"
#include "lua.h"

/* AHB-RAM linker symbols — the .audio/.ahb sections live here.
 * Free AHB space starts at __ahbram_end__ and extends to 0x30000000 + 128KB. */
extern "C" {
    extern uint32_t __ahbram_end__;
    extern uint16_t __AHBRAM_LENGTH__;
}

/* Initialize the secondary TLSF pool in unused AHB-RAM (~120KB).
 * AHB-RAM is uncached (DMA-safe), slightly slower than AXI-RAM but
 * perfectly fine for Lua table/string storage. This gives large
 * allocations (64KB+ string table resizes) an unfragmented region
 * separate from the main heap where small objects cause fragmentation. */
void p8_ahb_pool_setup(void) {
    uint32_t ahb_end_addr = (uint32_t)&__ahbram_end__;
    uint32_t ahb_region_end = 0x30000000 + (uint32_t)&__AHBRAM_LENGTH__;
    /* Align start to 8 bytes */
    ahb_end_addr = (ahb_end_addr + 7) & ~7u;
    if (ahb_end_addr < ahb_region_end) {
        size_t ahb_free = ahb_region_end - ahb_end_addr;
        printf("P8: AHB pool: %u KB at 0x%08X\n", (unsigned)(ahb_free / 1024), (unsigned)ahb_end_addr);
        p8_pool_init_ahb((void*)ahb_end_addr, ahb_free);
    }
}

/* Initialize SRD-SRAM pool (32KB @ 0x38000000).
 * This RAM region is completely unused by retro-go. Needs RCC clock enable. */
void p8_srd_pool_setup(void) {
    /* Enable SRD-SRAM clock */
    RCC->AHB4ENR |= RCC_AHB4ENR_SRDSRAMEN;
    __DSB();  /* ensure clock is enabled before accessing the RAM */

    void* srd_start = (void*)0x38000000;
    size_t srd_size = 32 * 1024;
    /* Quick sanity check: write and read back */
    *(volatile uint32_t*)srd_start = 0xDEADBEEF;
    if (*(volatile uint32_t*)srd_start == 0xDEADBEEF) {
        printf("P8: SRD pool: %u KB at 0x%08X\n", (unsigned)(srd_size / 1024), (unsigned)(uintptr_t)srd_start);
        p8_pool_init_srd(srd_start, srd_size);
    } else {
        printf("P8: SRD-SRAM not accessible!\n");
    }
}

/* Initialize ITCM pool from remaining ITCM after back_page + cart ROM.
 * ITCM is zero-wait-state RAM (480MHz) — fastest on the chip.
 * We reset the ITCM bump allocator first to reclaim space used by
 * retro-go's logo cache (not needed during gameplay). */
static uint8_t* embedded_cart_rom = NULL;  /* forward decl — used by ITCM setup + snapshot */

void p8_itcm_pool_setup(void) {
    extern void itc_init(void);
    extern void* itc_malloc(size_t size);

    /* Reset ITCM bump — frees logo cache and other menu allocations.
     * On exit, odroid_system_switch_app(0) reboots the device so
     * logos are re-cached from SD on next boot. */
    itc_init();

    /* Reserve 17KB for cart ROM snapshot (allocated later by p8_embedded_snapshot_rom).
     * Then give the rest to the ITCM pool.
     * After itc_init + back_page(16KB): 48KB free.
     * After cart_rom reserve(17KB): 31KB free → pool. */
    void *rom_reserve = itc_malloc(P8_ROM_SIZE);  /* 17KB reserved for cart ROM */
    if (rom_reserve == (void*)0xFFFFFFFF) rom_reserve = NULL;

    /* Grab remaining ITCM for pool */
    size_t try_size = 32 * 1024;
    void *itcm_block = itc_malloc(try_size);
    if (itcm_block == (void*)0xFFFFFFFF) {
        try_size = 16 * 1024;
        itcm_block = itc_malloc(try_size);
    }
    if (itcm_block == (void*)0xFFFFFFFF) {
        try_size = 8 * 1024;
        itcm_block = itc_malloc(try_size);
    }
    if (itcm_block != (void*)0xFFFFFFFF && itcm_block != NULL) {
        p8_pool_init_itcm(itcm_block, try_size);
    } else {
        printf("P8: ITCM pool: no space available\n");
    }

    /* Pre-assign the reserved block for cart ROM snapshot */
    if (rom_reserve) {
        embedded_cart_rom = (uint8_t*)rom_reserve;
    }
}

/* Constants */
#define P8_DISPLAY_FPS   60
#define P8_AUDIO_RATE    22050
#define P8_AUDIO_SAMPLES (P8_AUDIO_RATE / P8_DISPLAY_FPS)  /* 367 samples/frame at 60fps */
#define P8_WIDTH         128
#define P8_HEIGHT        128
#define LCD_WIDTH        320
#define LCD_HEIGHT       240
#define SCALE_W          240  /* 128 * 15/8 = 240 */
#define SCALE_H          240
#define OFFSET_X         ((LCD_WIDTH - SCALE_W) / 2)   /* 40 */
#define OFFSET_Y         ((LCD_HEIGHT - SCALE_H) / 2)  /* 0 */

/* ============================================================
 * Multicart support: load("#cart_name") searches SD card
 * ============================================================
 * Searches /roms/pico8/ and /roms/pico8/.multicarts/ for a file
 * whose name starts with cart_id and ends with .p8.png or .p8.
 * Returns 1 if found (out_path filled), 0 if not found.
 * ============================================================ */
/* sys_find_multicart is in p8_multicart.c (separate file to isolate ff.h) */
extern "C" int sys_find_multicart(const char* cart_id, char* out_path, int out_size);

/* Forward declarations */
void p8_embedded_snapshot_rom(void);

/* ============================================================
 * Cartdata persistence — read/write 256 bytes (64 dwords) to SD card.
 * Format: 64 lines of 8-char hex (matching PICO-8's .p8d.txt format).
 * Path: /data/pico8/cdata/<cartdata_id>.p8d.txt
 * ============================================================ */

/* Write 256 bytes of cartdata to SD card as hex text */
int sys_cartdata_write(const char* path, const uint8_t* data) {
    /* Ensure directory exists */
    odroid_sdcard_mkdir("/data/pico8/cdata");

    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cartdata write failed: %s\n", path);
        return -1;
    }
    for (int i = 0; i < 64; i++) {
        uint32_t bits = *(const uint32_t*)(data + i * 4);
        /* Use %08x (not %08lx) — newlib-nano compatible */
        fprintf(f, "%08x\n", (unsigned)bits);
    }
    fclose(f);
    printf("P8: cartdata saved to %s\n", path);
    return 0;
}

/* Read 256 bytes of cartdata from SD card. Returns 0 on success, -1 if not found. */
int sys_cartdata_read(const char* path, uint8_t* data) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;

    for (int i = 0; i < 64; i++) {
        unsigned val = 0;
        if (fscanf(f, "%x", &val) == 1) {
            *(uint32_t*)(data + i * 4) = (uint32_t)val;
        }
    }
    fclose(f);
    printf("P8: cartdata loaded from %s\n", path);
    return 0;
}

/* Flush cartdata to SD if dirty. Builds path from p8.cartdata_id (max 63 chars). */
static void gw_flush_cartdata(void) {
    if (!p8.cartdata_dirty || p8.cartdata_id[0] == '\0') return;
    char path[96];
    snprintf(path, sizeof(path), "/data/pico8/cdata/%s.p8d.txt", p8.cartdata_id);
    sys_cartdata_write(path, p8.ram + 0x5E00);
    p8.cartdata_dirty = false;
}

/* Audio buffer for one frame */
static int16_t p8_audio_buf[P8_AUDIO_SAMPLES];

/* ============================================================
 * sys_api.h implementation for Game & Watch backend
 * ============================================================ */

bool sys_init(void) { return true; }
void sys_shutdown(void) {}

void sys_draw_frame(const uint8_t* framebuffer, const uint32_t* palette) {
    /*
     * Single-pass render: back_page → screen mode → screen palette → RGB565 → scaled LCD.
     * Handles all PICO-8 display features: screen modes (0x5F2C), scanline palette
     * toggle (0x5F5F/0x5F70), secondary palette (0x5F60). No intermediate display_buf.
     */
    pixel_t* lcd = (pixel_t*)lcd_get_active_buffer();
    const uint8_t* bp = p8_back_page_ptr();

    /* Build RGB565 LUT for all 32 hardware palette entries */
    uint16_t hw565[32];
    for (int i = 0; i < 32; i++) {
        uint32_t rgb = palette[i];
        hw565[i] = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    }

    /* Build primary and secondary screen palette → RGB565 LUTs */
    uint16_t pal_primary[16], pal_secondary[16];
    for (int i = 0; i < 16; i++) {
        uint8_t sp = p8.ram[0x5F10 + i];
        uint8_t m = sp & 0x8F;
        pal_primary[i] = hw565[(m & 0x0F) + ((m & 0x80) ? 16 : 0)];

        sp = p8.ram[0x5F60 + i];
        m = sp & 0x8F;
        pal_secondary[i] = hw565[(m & 0x0F) + ((m & 0x80) ? 16 : 0)];
    }

    bool use_scanline_toggle = (p8.ram[0x5F5F] & 0x10) != 0;
    uint8_t mode = p8.ram[0x5F2C] & ~0x40;

    /* Precompute X/Y scale lookup tables (128→240) */
    static uint8_t lut_x[SCALE_W], lut_y[SCALE_H];
    static bool lut_init = false;
    if (!lut_init) {
        for (int i = 0; i < SCALE_W; i++) lut_x[i] = (uint8_t)(i * P8_WIDTH / SCALE_W);
        for (int i = 0; i < SCALE_H; i++) lut_y[i] = (uint8_t)(i * P8_HEIGHT / SCALE_H);
        lut_init = true;
    }

    /* Clear borders once */
    for (int y = 0; y < LCD_HEIGHT; y++) {
        pixel_t* row = lcd + y * LCD_WIDTH;
        for (int x = 0; x < OFFSET_X; x++) row[x] = 0;
        for (int x = OFFSET_X + SCALE_W; x < LCD_WIDTH; x++) row[x] = 0;
    }

    /* Main render loop */
    if (mode == 0 && !use_scanline_toggle) {
        /* Fast path: no screen mode, no scanline toggle (99% of carts) */
        for (int dy = 0; dy < SCALE_H; dy++) {
            const uint8_t* src_row = bp + lut_y[dy] * 128;
            pixel_t* dst_row = lcd + (OFFSET_Y + dy) * LCD_WIDTH + OFFSET_X;
            for (int dx = 0; dx < SCALE_W; dx++) {
                dst_row[dx] = pal_primary[src_row[lut_x[dx]] & 0x0F];
            }
        }
    } else {
        /* Full path: screen modes + scanline palette toggle */
        for (int dy = 0; dy < SCALE_H; dy++) {
            int p8_y = lut_y[dy];
            pixel_t* dst_row = lcd + (OFFSET_Y + dy) * LCD_WIDTH + OFFSET_X;

            const uint16_t* pal;
            if (use_scanline_toggle && ((p8.ram[0x5F70 + (p8_y >> 3)] >> (p8_y & 7)) & 1)) {
                pal = pal_secondary;
            } else {
                pal = pal_primary;
            }

            for (int dx = 0; dx < SCALE_W; dx++) {
                int p8_x = lut_x[dx];
                int src_x, src_y;

                if (mode & 0x80) {
                    switch (mode & 0x07) {
                        case 1: src_x = 127 - p8_x; src_y = p8_y; break;
                        case 2: src_x = p8_x; src_y = 127 - p8_y; break;
                        case 3: case 6: src_x = 127 - p8_x; src_y = 127 - p8_y; break;
                        case 5: src_x = p8_y; src_y = 127 - p8_x; break;
                        case 7: src_x = 127 - p8_y; src_y = p8_x; break;
                        default: src_x = p8_x; src_y = p8_y; break;
                    }
                } else {
                    bool sh = mode & 1, sv = mode & 2, mi = mode & 4;
                    if (mi) {
                        src_x = sh ? ((p8_x < 64) ? p8_x : (127 - p8_x)) : p8_x;
                        src_y = sv ? ((p8_y < 64) ? p8_y : (127 - p8_y)) : p8_y;
                    } else {
                        src_x = sh ? (p8_x / 2) : p8_x;
                        src_y = sv ? (p8_y / 2) : p8_y;
                    }
                }

                dst_row[dx] = pal[bp[src_y * 128 + src_x] & 0x0F];
            }
        }
    }
}

/* D-pad mouse-emulation toggle (host pause-menu item, see app_main_pico8). */
bool mouse_user_disabled = false;
bool mouse_menu_registered = false;
void mouse_toggle_action(void) {
    mouse_user_disabled = !mouse_user_disabled;
}

uint32_t sys_get_input_state(void) {
    uint32_t btns = buttons_get();
    uint32_t p8_btns = 0;
    if (btns & B_Left)  p8_btns |= (1 << 0);
    if (btns & B_Right) p8_btns |= (1 << 1);
    if (btns & B_Up)    p8_btns |= (1 << 2);
    if (btns & B_Down)  p8_btns |= (1 << 3);
    if (btns & B_A)     p8_btns |= (1 << 4);  /* O button */
    if (btns & B_B)     p8_btns |= (1 << 5);  /* X button */
    if (btns & B_PAUSE) p8_btns |= (1 << 16); /* Pause */
    return p8_btns;
}

uint32_t sys_get_time_ms(void) {
    return HAL_GetTick();
}

void sys_delay_ms(uint32_t ms) {
    HAL_Delay(ms);
}

bool sys_audio_init(void) { return true; }
void sys_audio_shutdown(void) {}
int sys_audio_buffer_size(void) { return P8_AUDIO_SAMPLES; }

/* On G&W, audio runs via DMA — no thread sync needed.
 * We fill the buffer synchronously in the main loop. */
void sys_audio_lock(void) {}
void sys_audio_unlock(void) {}
void sys_wdog_refresh(void) { wdog_refresh(); }

/* Override assert/abort to capture location before BSOD */
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("ASSERT: %s:%d %s() [%s]\n", file ? file : "?", line, func ? func : "?", expr ? expr : "?");
    /* Let it fall through to BSOD so crash screen shows the message */
    extern void abort(void);
    abort();
}

/* ============================================================
 * Audio resampling: 22050 Hz → 48000 Hz
 * ============================================================ */
static void resample_audio(const int16_t* src, int src_samples,
                           int16_t* dst, int dst_samples) {
    /* Linear interpolation upsampler */
    for (int i = 0; i < dst_samples; i++) {
        /* Map destination sample to source position (fixed-point) */
        uint32_t pos = (uint32_t)i * src_samples / dst_samples;
        uint32_t frac = ((uint32_t)i * src_samples * 256 / dst_samples) & 0xFF;
        int16_t s0 = src[pos];
        int16_t s1 = (pos + 1 < (uint32_t)src_samples) ? src[pos + 1] : s0;
        dst[i] = (int16_t)(s0 + ((int32_t)(s1 - s0) * frac >> 8));
    }
}

/* Repaint callback for the pause menu overlay */
static void p8_blit(void) {
    /* Clear audio during pause menu repaint (DMA keeps playing otherwise) */
    audio_clear_active_buffer();
    sys_draw_frame(p8_get_display_buffer(), p8_get_palette());
    common_ingame_overlay();
}

/* ============================================================
 * Main entry point
 * ============================================================ */
void app_main_pico8(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    (void)load_state; (void)start_paused; (void)save_slot;

    // Minimal logging to save logbuf space for runtime messages
    ram_start = (uint32_t) &_OVERLAY_PICO8_BSS_END;

    common_emu_state.frame_time_10us = (uint16_t)(100000 / P8_DISPLAY_FPS + 0.5f);
    odroid_system_init(APPID_PICO8, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(NULL, NULL, NULL, NULL, NULL);


    printf("P8: RAM free before init: %u KB\n", (unsigned)(ram_get_free_size() / 1024));

    p8_init(ACTIVE_FILE->path, false);
    /* Set loaded_cart for pause menu "reset cart" action */
    strncpy(p8.shell.loaded_cart, ACTIVE_FILE->path, sizeof(p8.shell.loaded_cart) - 1);
    p8.shell.loaded_cart[sizeof(p8.shell.loaded_cart) - 1] = '\0';
    wdog_refresh();

    printf("P8: pool=%uKB used=%uKB free=%uKB\n",
           (unsigned)(p8_pool_get_total() / 1024),
           (unsigned)(p8_pool_get_used() / 1024),
           (unsigned)(p8_pool_get_free() / 1024));

    p8_setup_coroutine();
    wdog_refresh();

    int audio_out_samples = AUDIO_SAMPLE_RATE / P8_DISPLAY_FPS;
    audio_clear_active_buffer();
    audio_clear_inactive_buffer();
    audio_start_playing(audio_out_samples);

    lcd_clear_active_buffer();
    lcd_clear_inactive_buffer();

    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_LAST
    };

    odroid_gamepad_state_t joystick;

    static int frame_num = 0;
    int skip_frame = 0;  /* For 30fps carts: skip VM on odd display frames */

    /* Main loop — always 60fps display. For 30fps carts, p8_step_frame
     * runs every other frame; the alternate frame re-displays the last output. */
    while (true) {
        wdog_refresh();
        uint32_t t0 = HAL_GetTick();

        common_emu_frame_loop();
        /* Prevent frame debt accumulation — PICO-8 frames can vary widely in cost
         * (6ms normal, 130ms+ death). Always reset to avoid permanent slowdown. */
        common_emu_state.skip_frames = 0;
        common_emu_state.pause_frames = 0;

        /* Read input through framework (handles PAUSE menu, turbo, etc.) */
        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &p8_blit);
        common_emu_input_loop_handle_turbo(&joystick);

        /* Map G&W buttons to PICO-8 */
        uint32_t p8_btns = 0;
        if (joystick.values[ODROID_INPUT_LEFT])  p8_btns |= (1 << 0);
        if (joystick.values[ODROID_INPUT_RIGHT]) p8_btns |= (1 << 1);
        if (joystick.values[ODROID_INPUT_UP])    p8_btns |= (1 << 2);
        if (joystick.values[ODROID_INPUT_DOWN])  p8_btns |= (1 << 3);
        if (joystick.values[ODROID_INPUT_A])     p8_btns |= (1 << 4);  /* O button */
        if (joystick.values[ODROID_INPUT_B])     p8_btns |= (1 << 5);  /* X button */

        /* Mouse emulation: when the cart enables devkit mode (poke(0x5F2D,1))
         * and the pause menu is NOT active, the D-pad drives the cursor with
         * acceleration; B = left click, A = right click. The directional and
         * face-button bits are stripped from p8_btns so the cart sees only
         * mouse events while in this mode. */
        {
            static int32_t mx_q8 = 64 << 8;     /* sub-pixel cursor pos, q8 */
            static int32_t my_q8 = 64 << 8;
            static int hold_x = 0;              /* signed frames-held counter */
            static int hold_y = 0;
            static bool mouse_was_active = false;
            static bool click_suppress = false;  /* require A/B release after re-entry */

            /* Mouse-emulation gating:
             *   - cart must enable devkit (poke 0x5F2D, 1)
             *   - cart must actually read mouse coords (p8.mouse_used) — some
             *     carts enable devkit only for keyboard input, where mouse
             *     remap would steal their D-pad
             *   - user hasn't disabled it via the pause menu toggle
             *   - pause menu must be closed (so menu navigation uses D-pad) */
            extern bool mouse_user_disabled;
            extern bool mouse_menu_registered;
            bool cart_uses_mouse = (p8.ram[0x5F2D] & 1) && p8.mouse_used;
            /* Re-register every frame so the label reflects the current state
             * (host item label is borrowed by reference; we point at one of
             * two static strings depending on mouse_user_disabled). */
            if (cart_uses_mouse) {
                p8_pause_menu_set_host_item(
                    mouse_user_disabled ? "dpad mouse: off" : "dpad mouse: on",
                    mouse_toggle_action);
                mouse_menu_registered = true;
            } else if (mouse_menu_registered) {
                p8_pause_menu_set_host_item(NULL, NULL);
                mouse_menu_registered = false;
            }
            bool mouse_mode = cart_uses_mouse && !mouse_user_disabled
                              && !p8_pause_menu_is_active();
            if (mouse_mode) {
                if (!mouse_was_active) {
                    /* Re-center on entry / pause menu close */
                    mx_q8 = (int32_t)p8.mouse_x << 8;
                    my_q8 = (int32_t)p8.mouse_y << 8;
                    if (mx_q8 == 0 && my_q8 == 0) {
                        mx_q8 = 64 << 8;
                        my_q8 = 64 << 8;
                    }
                    hold_x = hold_y = 0;
                    mouse_was_active = true;
                    /* Suppress phantom click from the A/B that closed the
                     * pause menu — require both buttons to be released first. */
                    click_suppress = true;
                }

                bool L = joystick.values[ODROID_INPUT_LEFT];
                bool R = joystick.values[ODROID_INPUT_RIGHT];
                bool U = joystick.values[ODROID_INPUT_UP];
                bool D = joystick.values[ODROID_INPUT_DOWN];

                /* Hold counters: positive = right/down, negative = left/up.
                 * Reverse direction resets to ±1 so the next press is slow. */
                if (L && !R)      hold_x = (hold_x > 0) ? -1 : hold_x - 1;
                else if (R && !L) hold_x = (hold_x < 0) ?  1 : hold_x + 1;
                else              hold_x = 0;
                if (U && !D)      hold_y = (hold_y > 0) ? -1 : hold_y - 1;
                else if (D && !U) hold_y = (hold_y < 0) ?  1 : hold_y + 1;
                else              hold_y = 0;

                /* Acceleration curve, q8 px/frame:
                 *   first frame   ~0.25 px (precise)
                 *   ~30 frames    ~3.0 px
                 *   capped       4.0 px
                 * Tap = 1px move, hold = smooth glide. */
                #define P8_MOUSE_STEP_Q8(hold) ({                          \
                    int _n = (hold) < 0 ? -(hold) : (hold);                \
                    int _q = 0;                                            \
                    if (_n > 0) {                                          \
                        _q = 64 + (_n - 1) * 24;                           \
                        if (_q > 1024) _q = 1024;                          \
                    }                                                      \
                    (hold) < 0 ? -_q : _q;                                 \
                })
                mx_q8 += P8_MOUSE_STEP_Q8(hold_x);
                my_q8 += P8_MOUSE_STEP_Q8(hold_y);
                #undef P8_MOUSE_STEP_Q8

                /* Clamp to PICO-8 screen 0..127 */
                if (mx_q8 < 0) mx_q8 = 0;
                if (my_q8 < 0) my_q8 = 0;
                if (mx_q8 > (127 << 8)) mx_q8 = 127 << 8;
                if (my_q8 > (127 << 8)) my_q8 = 127 << 8;

                p8.mouse_x = mx_q8 >> 8;
                p8.mouse_y = my_q8 >> 8;

                /* B = left click (bit 0), A = right click (bit 1) */
                bool a = joystick.values[ODROID_INPUT_A];
                bool b = joystick.values[ODROID_INPUT_B];
                if (click_suppress) {
                    if (!a && !b) click_suppress = false;
                    a = b = false;
                }
                int mb = 0;
                if (b) mb |= 1;
                if (a) mb |= 2;
                p8.mouse_btn = mb;

                /* Strip dpad + face-button bits so the cart only sees mouse */
                p8_btns &= ~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
                             (1 << 4) | (1 << 5));
            } else {
                if (mouse_was_active) {
                    p8.mouse_btn = 0;
                    mouse_was_active = false;
                }
            }
        }

        p8_set_button_state(p8_btns);

        /* Pause menu: toggle on GAME button (Mario G&W = ODROID_INPUT_START)
         * or START button (Zelda G&W = ODROID_INPUT_X).
         * PAUSE/VOLUME is consumed by retro-go for its system menu. */
        {
            static bool prev_pause_btn = false;
            bool pause_now = joystick.values[ODROID_INPUT_START]  /* GAME (Mario) */
                          || joystick.values[ODROID_INPUT_X];     /* START (Zelda) */
            if (pause_now && !prev_pause_btn) {
                if (p8_pause_menu_is_active())
                    p8_pause_menu_close();
                else
                    p8_pause_menu_open();
            }
            prev_pause_btn = pause_now;
        }
        if (p8_pause_menu_is_active()) {
            /* Edge-detect for menu navigation */
            static uint32_t prev_menu = 0;
            static bool menu_suppress = true;
            if (menu_suppress) {
                prev_menu = p8_btns | (1 << 4) | (1 << 5) | (1 << 6);
                menu_suppress = false;
                printf("P8: pause menu opened, btns=0x%x\n", (unsigned)p8_btns);
            }
            uint32_t newly = p8_btns & ~prev_menu;
            prev_menu = p8_btns;
            if (newly) printf("P8: menu newly=0x%x btns=0x%x\n", (unsigned)newly, (unsigned)p8_btns);
            p8_pause_menu_update(newly);
            if (p8_pause_menu_is_active()) {
                p8_pause_menu_draw();
                /* Reset screen palette to identity so menu colors (7=white
                 * etc.) are not remapped by the cart's custom palette.
                 * sys_draw_frame maps back_page through 0x5F10. */
                uint8_t saved_pal[16];
                memcpy(saved_pal, &p8.ram[0x5F10], 16);
                for (int i = 0; i < 16; i++) p8.ram[0x5F10 + i] = i;
                /* Blit to BOTH LCD buffers so menu is visible regardless
                 * of which buffer the LCD is currently displaying */
                p8_blit();
                lcd_swap();
                p8_blit();
                /* Restore cart's screen palette */
                memcpy(&p8.ram[0x5F10], saved_pal, 16);
            } else {
                menu_suppress = true;
                printf("P8: pause menu closed\n");
                /* Check if "reset cart" was selected */
                if (p8.next_cart_path[0] != '\0') {
                    printf("P8: resetting cart\n");
                    p8.next_cart_path[0] = '\0';
                    /* Flush save data */
                    if (p8.cartdata_dirty) gw_flush_cartdata();
                    /* Close Lua state to free all Lua allocations */
                    if (p8.L) { lua_close(p8.L); p8.L = NULL; p8.cart_co = NULL; }
                    /* Reset frozen globals (allocated from pool, not freed by lua_close) */
                    { extern void luaH_frozen_reset(void); luaH_frozen_reset(); }
                    /* Full re-init from scratch (p8_pool_setup resets the main pool) */
                    p8_init(ACTIVE_FILE->path, false);
                    wdog_refresh();
                    p8_setup_coroutine();
                    wdog_refresh();
                    p8_embedded_snapshot_rom();
                    skip_frame = 0;
                    strncpy(p8.shell.loaded_cart, ACTIVE_FILE->path, sizeof(p8.shell.loaded_cart) - 1);
                }
            }
            continue;
        }

        /* For 30fps carts: run VM every other display frame.
         * 60fps carts run every frame. Audio always fills every frame
         * (the audio engine ticks independently of the Lua frame). */
        bool run_vm = true;
        if (p8.target_fps <= 30) {
            skip_frame ^= 1;
            run_vm = !skip_frame;
        }

        uint32_t t1 = HAL_GetTick();
        if (run_vm) {
            if (!p8_step_frame()) {
                const char* err = p8_get_last_error();

                /* Check for multicart load request */
                if (strncmp(err, "_p8_load:", 9) == 0) {
                    const char* cart_path = err + 9;
                    printf("P8: loading multicart: %s\n", cart_path);

                    /* Flush cartdata before switching carts */
                    if (p8.cartdata_dirty) gw_flush_cartdata();

                    /* Cache the new cart from SD into flash (shows progress bar) */
                    uint32_t new_size = 0;
                    wdog_refresh();
                    uint8_t* new_data = odroid_overlay_cache_file_in_flash(
                        cart_path, &new_size, false);
                    wdog_refresh();

                    if (new_data && new_size > 0) {
                        /* Update ROM_DATA to point to new cart in flash */
                        ROM_DATA = new_data;
                        ROM_DATA_LENGTH = new_size;

                        /* Save param string before p8_init resets P8_State.
                         * The new cart reads it via stat(6) for state transfer. */
                        char saved_param[256];
                        strncpy(saved_param, p8.next_param_str, sizeof(saved_param) - 1);
                        saved_param[sizeof(saved_param) - 1] = '\0';

                        /* Reinitialize engine with soft_load=true (matches PICO-8).
                         * soft_load preserves RAM above 0x4300 (upper spritesheet,
                         * general purpose memory) — needed for multicarts that share
                         * data via upper memory (e.g. snekburd's extra spritesheet). */
                        p8_init(cart_path, true);
                        wdog_refresh();

                        /* Restore param string so new cart can read via stat(6) */
                        strncpy(p8.next_param_str, saved_param, sizeof(p8.next_param_str) - 1);
                        p8.next_param_str[sizeof(p8.next_param_str) - 1] = '\0';

                        p8_setup_coroutine();
                        wdog_refresh();

                        /* Reset frame skip state */
                        skip_frame = 0;

                        /* Snapshot ROM for reload() */
                        p8_embedded_snapshot_rom();

                        printf("P8: multicart loaded OK, param='%s'\n", p8.next_param_str);
                        continue;  /* back to main loop */
                    } else {
                        printf("P8: multicart load failed: %s\n", cart_path);
                        /* Show alert with cart name, then return to menu */
                        lcd_clear_active_buffer();
                        odroid_overlay_draw_text(4, 4, LCD_WIDTH - 8,
                            "Multicart not found:", C_GW_YELLOW, C_GW_RED);
                        odroid_overlay_draw_text(4, 20, LCD_WIDTH - 8,
                            cart_path, 0xFFFF, C_GW_RED);
                        odroid_overlay_draw_text(4, 44, LCD_WIDTH - 8,
                            "Place cart file in", C_GW_YELLOW, C_GW_RED);
                        odroid_overlay_draw_text(4, 56, LCD_WIDTH - 8,
                            "/roms/pico8/ or", C_GW_YELLOW, C_GW_RED);
                        odroid_overlay_draw_text(4, 68, LCD_WIDTH - 8,
                            "/roms/pico8/.multicarts/", C_GW_YELLOW, C_GW_RED);
                        odroid_overlay_draw_text(4, 92, LCD_WIDTH - 8,
                            "Press any button...", 0xFFFF, C_GW_RED);
                        lcd_swap();
                        odroid_gamepad_state_t js;
                        do { wdog_refresh(); HAL_Delay(50); odroid_input_read_gamepad(&js);
                        } while (!js.values[ODROID_INPUT_A] && !js.values[ODROID_INPUT_B] &&
                                 !js.values[ODROID_INPUT_START]);
                        odroid_system_switch_app(0);
                    }
                }

                printf("P8 STOPPED: %s\n", err);
                /* Show error on screen, wait for any button, then return to menu */
                lcd_clear_active_buffer();
                odroid_overlay_draw_text(4, 4, LCD_WIDTH - 8, "PICO-8 Error:", C_GW_YELLOW, C_GW_RED);
                odroid_overlay_draw_text(4, 20, LCD_WIDTH - 8, err, 0xFFFF, C_GW_RED);
                char mem_info[128];
                sprintf(mem_info, "pool=%uKB used=%uKB free=%uKB",
                        (unsigned)(p8_pool_get_total() / 1024),
                        (unsigned)(p8_pool_get_used() / 1024),
                        (unsigned)(p8_pool_get_free() / 1024));
                odroid_overlay_draw_text(4, 44, LCD_WIDTH - 8, mem_info, C_GW_YELLOW, C_GW_RED);
                odroid_overlay_draw_text(4, 68, LCD_WIDTH - 8, "Press any button...", 0xFFFF, C_GW_RED);
                lcd_swap();
                /* Wait for button press */
                odroid_gamepad_state_t js;
                do { wdog_refresh(); HAL_Delay(50); odroid_input_read_gamepad(&js);
                } while (!js.values[ODROID_INPUT_A] && !js.values[ODROID_INPUT_B] &&
                         !js.values[ODROID_INPUT_START]);
                /* Flush cartdata before exiting */
                if (p8.cartdata_dirty) gw_flush_cartdata();
                odroid_system_switch_app(0);
            }
        }

        /* Audio — fill every display frame (audio ticks independently).
         * 22050/60 = 367.5 samples/frame — Bresenham accumulator for exact rate. */
        if (!common_emu_sound_loop_is_muted()) {
            static int audio_frac = 0;
            int p8_samples = P8_AUDIO_RATE / P8_DISPLAY_FPS;  /* 367 */
            audio_frac += P8_AUDIO_RATE % P8_DISPLAY_FPS;      /* +30 each frame */
            if (audio_frac >= P8_DISPLAY_FPS) {
                audio_frac -= P8_DISPLAY_FPS;
                p8_samples++;  /* 368 this frame */
            }
            p8_fill_audio(p8_audio_buf, p8_samples);
            int32_t vol = common_emu_sound_get_volume();
            int16_t* audio_out = audio_get_active_buffer();
            resample_audio(p8_audio_buf, p8_samples,
                           audio_out, audio_out_samples);
            uint16_t buf_len = audio_get_buffer_length();
            for (int i = 0; i < buf_len; i++) {
                audio_out[i] = (int16_t)(((int32_t)audio_out[i] * vol) >> 8);
            }
        }

        /* Display — always present (re-displays last frame for 30fps skip) */
        uint32_t t3 = HAL_GetTick();
        sys_draw_frame(p8_get_display_buffer(), p8_get_palette());
        common_ingame_overlay();
        lcd_swap();
        /* Vsync disabled — testing audio stutter */
        // lcd_wait_for_vblank();
        uint32_t t4 = HAL_GetTick();

        /* Periodic tasks: every 60 frames (~1 sec at 60fps) */
        if (++frame_num % 60 == 0) {
            /* Debounced cartdata flush — at most once per second, not per dset() */
            if (p8.cartdata_dirty) gw_flush_cartdata();

            uint32_t tot = t4 - t0;
            uint32_t t_step = t3 - t1;
            printf("F%d vm=%lu dr=%lu tot=%lu | mem=%u/%uKB free=%uKB",
                   frame_num,
                   (unsigned long)t_step,
                   (unsigned long)(t4 - t3),
                   (unsigned long)tot,
                   (unsigned)(p8_pool_get_used() / 1024),
                   (unsigned)(p8_pool_get_total() / 1024),
                   (unsigned)(p8_pool_get_free() / 1024));
            if (p8.L) {
                int gc_kb = lua_gc(p8.L, LUA_GCCOUNT, 0);
                int gc_b  = lua_gc(p8.L, LUA_GCCOUNTB, 0);
                printf(" gc=%d.%dKB base=%d", gc_kb, gc_b / 100, p8.base_fps);
            }
            printf("\n");
        }

        /* Stop-the-world GC: full collect between frames.
         * Incremental GCSTEP caused crashes (bubble_bobble, pico_ball) —
         * the restart/stop cycle corrupts GC state with paged allocator. */
        if (p8.L) {
            lua_gc(p8.L, LUA_GCCOLLECT, 0);
        }

        /* Frame pacing: reset skip/pause to prevent integrator debt buildup */
        common_emu_state.pause_frames = 0;
        common_emu_state.skip_frames = 0;
        common_emu_sound_sync(false);
    }
}

/* ============================================================
 * Platform memory wrappers — G&W uses pool allocator
 * ============================================================ */
#include "p8_alloc.h"

void* p8_malloc(size_t size) { return p8_pool_malloc(size); }
void  p8_free(void* ptr) { p8_pool_free(ptr); }
void* p8_realloc(void* ptr, size_t size) { return p8_pool_realloc(ptr, size); }
void* p8_calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = p8_pool_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

/* ============================================================
 * Cart loading — G&W loads from flash memory (ROM_DATA)
 * ============================================================ */

/* Phase 1: extract cart data (PNG decode + decompress) without Lua.
 * Called BEFORE p8_lua_init so stbi gets a clean, unfragmented heap.
 * Returns Lua source via out params; caller compiles it after lua_init. */
int p8_cart_extract_png(const uint8_t* data, size_t data_len, bool soft_load,
                        uint8_t** out_code, int* out_len);

int p8_cart_extract(const char* path, bool soft_load,
                    uint8_t** out_code, int* out_len) {
    (void)path;
    const uint8_t* data = ROM_DATA;
    uint32_t data_len = ROM_DATA_LENGTH;

    if (!data || data_len == 0) {
        fprintf(stderr, "p8_cart_extract: no ROM data available\n");
        return -1;
    }

    /* PNG signature: 0x89 'P' 'N' 'G' */
    if (data_len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return p8_cart_extract_png(data, data_len, soft_load, out_code, out_len);
    } else {
        /* .p8 text: no stbi needed, load normally after lua_init */
        *out_code = NULL;
        *out_len = 0;
        return 1;  /* signal: use sys_load_cart instead (text format) */
    }
}

int sys_load_cart(const char* path, bool soft_load) {
    /* Cart data is already in flash via ROM_DATA (cached by firmware).
     * Detect PNG vs .p8 text by checking the PNG signature. */
    const uint8_t* data = ROM_DATA;
    uint32_t data_len = ROM_DATA_LENGTH;

    if (!data || data_len == 0) {
        fprintf(stderr, "sys_load_cart: no ROM data available\n");
        return -1;
    }

    /* PNG signature: 0x89 'P' 'N' 'G' */
    if (data_len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return p8_cart_load_png_from_memory(data, data_len, soft_load);
    } else {
        /* Treat as .p8 text */
        return p8_cart_load_from_string((const char*)data, data_len, soft_load);
    }
}

/* ============================================================
 * reload() support — on embedded, cart_rom is a pool allocation
 * (not BSS) to save 17KB that grows the pool. Snapshot taken after
 * cart loading when pool has spare capacity.
 * ============================================================ */
/* embedded_cart_rom declared earlier (before p8_itcm_pool_setup) */

/* Called after successful cart load to snapshot p8.ram[0..0x42FF].
 * Allocates from ITCM (fast, keeps main pool free). */
void p8_embedded_snapshot_rom(void) {
    if (!embedded_cart_rom) {
        void* ptr = itc_calloc(1, P8_ROM_SIZE);
        if (ptr != (void*)0xffffffff) {
            embedded_cart_rom = (uint8_t*)ptr;
        } else {
            /* ITCM full — fall back to pool */
            embedded_cart_rom = (uint8_t*)p8_pool_malloc(P8_ROM_SIZE);
        }
    }
    if (embedded_cart_rom) {
        memcpy(embedded_cart_rom, p8.ram, P8_ROM_SIZE);
        printf("P8: cart_rom snapshot at 0x%08X (%u bytes)\n",
               (unsigned)(uintptr_t)embedded_cart_rom, (unsigned)P8_ROM_SIZE);
    }
}

/* Called from api_reload to restore original ROM data */
int sys_reload_cart_rom(uint32_t dest_addr, uint32_t src_addr, uint32_t len) {
    if (embedded_cart_rom) {
        memcpy(p8.ram + dest_addr, embedded_cart_rom + src_addr, len);
        return 0;
    }
    fprintf(stderr, "sys_reload_cart_rom: no snapshot available\n");
    return -1;
}

/* ============================================================
 * Date/time — G&W uses hardware RTC
 * ============================================================ */
#include "rg_rtc.h"

void sys_get_datetime(int* year, int* month, int* day,
                      int* hour, int* minute, int* second, int utc) {
    /* G&W RTC is local time only (no timezone support), ignore utc flag */
    (void)utc;
    if (year)   *year   = 2000 + GW_GetCurrentYear();
    if (month)  *month  = GW_GetCurrentMonth();
    if (day)    *day    = GW_GetCurrentDay();
    if (hour)   *hour   = GW_GetCurrentHour();
    if (minute) *minute = GW_GetCurrentMinute();
    if (second) *second = GW_GetCurrentSecond();
}
