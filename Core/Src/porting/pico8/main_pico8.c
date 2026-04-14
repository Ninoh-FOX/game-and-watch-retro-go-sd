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
    /* ITCM bump allocator (gw_malloc.c) */
    void itc_init(void);
    void* itc_malloc(size_t size);
    void* itc_calloc(size_t count, size_t size);
    /* ITCM sentinel patching (set by rg_emulators.c before app_main_pico8) */
    extern uint8_t *pico8_code_flash_addr;
    extern uint32_t pico8_code_flash_size;
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

/* Initialize ITCM (zero-wait-state RAM, 480MHz):
 * 1. Load hot code from /cores/pico8_itcm.bin (lvm, ltable, lgc, lapi, ldo, p8_render)
 * 2. Patch sentinel addresses (0xBEEF...) to real QSPI XIP addresses
 * 3. Allocate back_page (16KB) in remaining ITCM space
 * cart_rom is allocated from main pool (AXI SRAM) — only used by reload(). */
static uint8_t* embedded_cart_rom = NULL;  /* allocated in main pool by p8_embedded_snapshot_rom */

static int itcm_setup_done = 0;
static size_t itcm_loaded_size = 0;  /* actual bytes loaded (includes linker stubs) */

void p8_itcm_init(void) {

    /* Load ITCM hot code from SD card.
     * Always reload — multicart switches re-enter this function.
     * After loading, patch sentinel addresses (0xBEEF0000 range) in the ITCM
     * code to point to the real QSPI XIP address, same as overlay patching. */
    {
        extern uint32_t __itcram_hot_start__;
        uint32_t dst = (uint32_t)&__itcram_hot_start__;
        FILE *f = fopen("/cores/pico8_itcm.bin", "rb");
        if (f) {
            /* Read full file — includes linker stubs beyond __itcram_hot_end__ */
            fseek(f, 0, SEEK_END);
            size_t file_size = ftell(f);
            fseek(f, 0, SEEK_SET);
            size_t read = fread((void*)dst, 1, file_size, f);
            fclose(f);
            itcm_loaded_size = read;
            printf("P8: ITCM hot code loaded from SD: %u bytes to 0x%08lx\n",
                   (unsigned)read, dst);

            /* Patch sentinel refs in ITCM (veneers targeting pico8 XIP code).
             * pico8_code_flash_addr set by rg_emulators.c before app_main_pico8. */
            if (pico8_code_flash_addr && pico8_code_flash_size > 0) {
                int32_t offset = (uint32_t)pico8_code_flash_addr - 0xBEEF0000;
                int patched = 0;
                /* NOTE: dst can be 0 (ITCM starts at address 0x00000000).
                 * Use volatile to prevent the compiler from optimizing away
                 * the loop as NULL-pointer UB. */
                volatile uint32_t *start = (volatile uint32_t*)dst;
                volatile uint32_t *end = (volatile uint32_t*)(dst + read);
                for (volatile uint32_t *ptr = start; ptr < end; ptr++) {
                    uint32_t value = *ptr;
                    if ((value & ~1u) >= 0xBEEF0000 &&
                        (value & ~1u) < 0xBEEF0000 + pico8_code_flash_size) {
                        *(uint32_t*)ptr = value + offset;
                        patched++;
                    }
                }
                printf("P8: ITCM patched %d sentinel refs\n", patched);
            } else {
                printf("P8: ITCM sentinel patch skipped (flash_addr=%p)\n",
                       (void*)pico8_code_flash_addr);
            }
        } else {
            printf("P8: WARNING: /cores/pico8_itcm.bin not found — VM runs from AXI SRAM\n");
        }
    }

    /* Only do back_page allocation on first call. Multicart reloads reuse
     * the existing back_page — itc_init() would reset the bump pointer
     * and cause a fresh allocation to overlap back_page. */
    if (itcm_setup_done)
        return;
    itcm_setup_done = 1;

    /* Allocate back_page (16KB) in ITCM after the loaded hot code.
     * - itc_init() resets the shared bump allocator to __itcram_end__,
     *   reclaiming any space retro-go used (e.g. logo cache).
     * - If loaded ITCM extends past __itcram_end__ (linker stubs in the
     *   1KB padding), advance the bump to skip them.
     * - Then itc_calloc places back_page at the resulting address.
     * ITCM starts at 0x00000000, so valid pointers CAN be 0.
     * itc_calloc returns 0xFFFFFFFF on failure → fall back to main pool. */
    if (p8_back_page_ptr() == (uint8_t*)0xFFFFFFFF) {
        extern uint32_t __itcram_hot_start__;
        extern uint32_t __itcram_end__;
        uint32_t code_end = (uint32_t)&__itcram_hot_start__ + itcm_loaded_size;
        itc_init();
        if (code_end > (uint32_t)&__itcram_end__) {
            itc_malloc(code_end - (uint32_t)&__itcram_end__);
        }
        void* itc_ptr = itc_calloc(1, 128 * 128);
        if (itc_ptr != (void*)0xFFFFFFFF) {
            p8_set_back_page((uint8_t*)itc_ptr);
            printf("P8: back_page in ITCM at 0x%08lX (16KB)\n", (unsigned long)(uintptr_t)itc_ptr);
        } else {
            uint8_t* bp = (uint8_t*)p8_pool_malloc(128 * 128);
            if (bp) memset(bp, 0, 128 * 128);
            p8_set_back_page(bp);
            printf("P8: back_page in pool at 0x%08lX (16KB)\n", (unsigned long)(uintptr_t)bp);
        }
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

/* Scaling modes — mapped from retro-go's odroid_display_scaling_t.
 *   OFF:    128×128 centered (offset 96,56), no scaling
 *   FIT:    240×240 centered (offset 40, 0), aspect-preserved 15/8 scale
 *   FULL:   256×240          (offset 32, 0), 2x pixel double with 8 output
 *                            rows cropped top+bottom (source y=4..123 shown)
 *   CUSTOM: same as FIT (reserved for future use)
 * Default on first PICO-8 launch is FIT; user can change via the retro-go
 * system-pause "Scaling" option and the choice persists per-app. */
#define MAX_SCALE_W      256
#define MAX_SCALE_H      240

static uint16_t cur_scale_w, cur_scale_h, cur_offset_x, cur_offset_y;
static uint8_t  scale_lut_x[MAX_SCALE_W];
static uint8_t  scale_lut_y[MAX_SCALE_H];
static odroid_display_scaling_t cur_scaling = (odroid_display_scaling_t)-1;

/* Invalidated on scaling change so the full-LCD memset runs again (old
 * border content would otherwise remain visible when switching to a mode
 * with smaller active area). */
static pixel_t *scale_cleared_bufs[2] = { NULL, NULL };

/* src_x/src_y LUTs for the screen-mode "Full path" — file-scope so the
 * scaling-mode rebuild can invalidate them together with scale_lut_*. */
static uint8_t scale_src_x_lut[MAX_SCALE_W];
static uint8_t scale_src_y_lut[MAX_SCALE_H];
static uint8_t scale_cached_mode = 0xFF;

/* Resolve the current scaling mode into scale_w/h + offsets + LUTs. No-op
 * when the mode hasn't changed since the last call. Call at the top of
 * any function that reads cur_scale_w/h/offset — cheap (one int compare
 * in the steady state). */
static void pico8_scaling_sync(void)
{
    odroid_display_scaling_t m = odroid_display_get_scaling_mode();
    if (m == cur_scaling) return;
    cur_scaling = m;
    switch (m) {
    case ODROID_DISPLAY_SCALING_OFF:
        cur_scale_w = 128; cur_scale_h = 128;
        cur_offset_x = (LCD_WIDTH  - 128) / 2;  /* 96 */
        cur_offset_y = (LCD_HEIGHT - 128) / 2;  /* 56 */
        for (int i = 0; i < 128; i++) scale_lut_x[i] = (uint8_t)i;
        for (int i = 0; i < 128; i++) scale_lut_y[i] = (uint8_t)i;
        break;
    case ODROID_DISPLAY_SCALING_FULL:
        /* 2x pixel doubling in both axes → virtual 256×256, clipped to
         * 240 by shifting input by 8 output rows (= 4 source rows). */
        cur_scale_w = 256; cur_scale_h = 240;
        cur_offset_x = (LCD_WIDTH - 256) / 2;   /* 32 */
        cur_offset_y = 0;
        for (int i = 0; i < 256; i++) scale_lut_x[i] = (uint8_t)(i / 2);
        for (int i = 0; i < 240; i++) scale_lut_y[i] = (uint8_t)((i + 8) / 2);
        break;
    case ODROID_DISPLAY_SCALING_FIT:
    case ODROID_DISPLAY_SCALING_CUSTOM:
    default:
        cur_scale_w = 240; cur_scale_h = 240;
        cur_offset_x = (LCD_WIDTH  - 240) / 2;  /* 40 */
        cur_offset_y = 0;
        for (int i = 0; i < 240; i++) scale_lut_x[i] = (uint8_t)(i * P8_WIDTH  / 240);
        for (int i = 0; i < 240; i++) scale_lut_y[i] = (uint8_t)(i * P8_HEIGHT / 240);
        break;
    }
    /* Per-mode caches depend on the LUT contents → invalidate. */
    scale_cleared_bufs[0] = scale_cleared_bufs[1] = NULL;
    scale_cached_mode = 0xFF;  /* forces src_x/y LUT rebuild in slow path */
}

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

    /* RGB565 LUT for the 32 hardware palette entries.
     * PICO-8's hardware palette is fixed (16 standard + 16 secret colors),
     * so we cache the conversion. Recompute only if the palette pointer
     * changes (e.g. shell uses a different palette than carts). */
    static uint16_t hw565[32];
    static const uint32_t* hw565_src = NULL;
    if (palette != hw565_src) {
        for (int i = 0; i < 32; i++) {
            uint32_t rgb = palette[i];
            hw565[i] = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
        }
        hw565_src = palette;
    }

    /* Build primary and secondary screen palette → RGB565 LUTs.
     * Cached: rebuild only when 0x5F10/0x5F60 RAM bytes change OR hw LUT changes.
     * Carts typically set the screen palette at init/scene transitions, not
     * per frame — so this is a no-op most frames. */
    static uint16_t pal_primary[16], pal_secondary[16];
    static uint8_t cached_primary[16], cached_secondary[16];
    static const uint32_t* pal_cache_hw_src = NULL;
    if (pal_cache_hw_src != hw565_src ||
        memcmp(cached_primary, &p8.ram[0x5F10], 16) != 0) {
        for (int i = 0; i < 16; i++) {
            uint8_t sp = p8.ram[0x5F10 + i];
            uint8_t m = sp & 0x8F;
            pal_primary[i] = hw565[(m & 0x0F) + ((m & 0x80) ? 16 : 0)];
            cached_primary[i] = sp;
        }
    }
    if (pal_cache_hw_src != hw565_src ||
        memcmp(cached_secondary, &p8.ram[0x5F60], 16) != 0) {
        for (int i = 0; i < 16; i++) {
            uint8_t sp = p8.ram[0x5F60 + i];
            uint8_t m = sp & 0x8F;
            pal_secondary[i] = hw565[(m & 0x0F) + ((m & 0x80) ? 16 : 0)];
            cached_secondary[i] = sp;
        }
    }
    pal_cache_hw_src = hw565_src;

    /* Pick up any runtime scaling-mode change from retro-go's pause menu. */
    pico8_scaling_sync();

    bool use_scanline_toggle = (p8.ram[0x5F5F] & 0x10) != 0;
    uint8_t mode = p8.ram[0x5F2C] & ~0x40;
    const int sw = cur_scale_w, sh = cur_scale_h;
    const int ox = cur_offset_x, oy = cur_offset_y;

    /* Clear once per LCD buffer with a single memset. The center PICO-8 area
     * gets overwritten by the render loop below; the borders stay 0. memset
     * is faster than per-row border loops (AXI burst stores). Invalidated
     * on scaling-mode change so the new border area gets cleared. */
    if (lcd != scale_cleared_bufs[0] && lcd != scale_cleared_bufs[1]) {
        memset(lcd, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(pixel_t));
        if (scale_cleared_bufs[0] == NULL) scale_cleared_bufs[0] = lcd;
        else                               scale_cleared_bufs[1] = lcd;
    }

    /* Main render loop */
    if (mode == 0 && !use_scanline_toggle) {
        /* Fast path: no screen mode, no scanline toggle (99% of carts) */
        for (int dy = 0; dy < sh; dy++) {
            const uint8_t* src_row = bp + scale_lut_y[dy] * 128;
            pixel_t* dst_row = lcd + (oy + dy) * LCD_WIDTH + ox;
            for (int dx = 0; dx < sw; dx++) {
                dst_row[dx] = pal_primary[src_row[scale_lut_x[dx]] & 0x0F];
            }
        }
    } else {
        /* Full path: screen modes + scanline palette toggle.
         * PICO-8 reference (Ghidra blit_pico8_back_page_to_pico8_screen):
         * palette-per-scanline is resolved using the SOURCE row (pre-mode),
         * then screen mode is applied in a separate in-place pass. So we
         * index 0x5F70 by src_y, not p8_y — matches SDL2 backend.
         *
         * Mode is frame-constant, so we hoist the per-axis transform into
         * LUTs and rebuild only when mode changes. Inner loops are then as
         * tight as the fast path (single table lookup per pixel).
         *
         * Non-rotation (modes 0-3, 6 + stretch/mirror family):
         *   src_x = f(p8_x) only, src_y = f(p8_y) only
         *   → palette pick hoisted to outer loop (src_y row-constant)
         *
         * Rotation (modes 5, 7 — 90° rotations): axes swap:
         *   src_x = f(p8_y), src_y = f(p8_x)
         *   → palette pick stays inner (src_y varies per pixel) */
        bool rotation = (mode & 0x80) && ((mode & 0x07) == 5 || (mode & 0x07) == 7);

        if (mode != scale_cached_mode) {
            scale_cached_mode = mode;
            if (rotation) {
                /* Mode 5: src_x = p8_y, src_y = 127 - p8_x (90° CCW)
                 * Mode 7: src_x = 127 - p8_y, src_y = p8_x (90° CW) */
                bool m5 = (mode & 0x07) == 5;
                for (int dy = 0; dy < sh; dy++) {
                    int p8_y = scale_lut_y[dy];
                    scale_src_x_lut[dy] = m5 ? p8_y : (127 - p8_y);
                }
                for (int dx = 0; dx < sw; dx++) {
                    int p8_x = scale_lut_x[dx];
                    scale_src_y_lut[dx] = m5 ? (127 - p8_x) : p8_x;
                }
            } else {
                /* Y axis */
                bool flip_y, stretch_y, mirror_y;
                if (mode & 0x80) {
                    flip_y = (mode & 7) == 2 || (mode & 7) == 3 || (mode & 7) == 6;
                    stretch_y = mirror_y = false;
                } else {
                    flip_y = false;
                    stretch_y = (mode & 2) && !(mode & 4);
                    mirror_y  = (mode & 2) &&  (mode & 4);
                }
                for (int dy = 0; dy < sh; dy++) {
                    int p8_y = scale_lut_y[dy];
                    int src_y;
                    if (flip_y)         src_y = 127 - p8_y;
                    else if (stretch_y) src_y = p8_y / 2;
                    else if (mirror_y)  src_y = (p8_y < 64) ? p8_y : (127 - p8_y);
                    else                src_y = p8_y;
                    scale_src_y_lut[dy] = src_y;
                }
                /* X axis */
                bool flip_x, stretch_x, mirror_x;
                if (mode & 0x80) {
                    flip_x = (mode & 7) == 1 || (mode & 7) == 3 || (mode & 7) == 6;
                    stretch_x = mirror_x = false;
                } else {
                    flip_x = false;
                    stretch_x = (mode & 1) && !(mode & 4);
                    mirror_x  = (mode & 1) &&  (mode & 4);
                }
                for (int dx = 0; dx < sw; dx++) {
                    int p8_x = scale_lut_x[dx];
                    int src_x;
                    if (flip_x)         src_x = 127 - p8_x;
                    else if (stretch_x) src_x = p8_x / 2;
                    else if (mirror_x)  src_x = (p8_x < 64) ? p8_x : (127 - p8_x);
                    else                src_x = p8_x;
                    scale_src_x_lut[dx] = src_x;
                }
            }
        }

        if (!rotation) {
            /* Fast hoisted form: inner loop identical cost to fast path */
            for (int dy = 0; dy < sh; dy++) {
                int src_y = scale_src_y_lut[dy];
                const uint8_t* src_row = bp + src_y * 128;
                const uint16_t* pal;
                if (use_scanline_toggle &&
                    ((p8.ram[0x5F70 + (src_y >> 3)] >> (src_y & 7)) & 1)) {
                    pal = pal_secondary;
                } else {
                    pal = pal_primary;
                }
                pixel_t* dst_row = lcd + (oy + dy) * LCD_WIDTH + ox;
                for (int dx = 0; dx < sw; dx++) {
                    dst_row[dx] = pal[src_row[scale_src_x_lut[dx]] & 0x0F];
                }
            }
        } else {
            /* Rotation: src_y varies per pixel → palette pick stays inner */
            for (int dy = 0; dy < sh; dy++) {
                int src_x = scale_src_x_lut[dy];
                pixel_t* dst_row = lcd + (oy + dy) * LCD_WIDTH + ox;
                for (int dx = 0; dx < sw; dx++) {
                    int src_y = scale_src_y_lut[dx];
                    const uint16_t* pal;
                    if (use_scanline_toggle &&
                        ((p8.ram[0x5F70 + (src_y >> 3)] >> (src_y & 7)) & 1)) {
                        pal = pal_secondary;
                    } else {
                        pal = pal_primary;
                    }
                    dst_row[dx] = pal[bp[src_y * 128 + src_x] & 0x0F];
                }
            }
        }
    }
}

/* Render a 128x128 framebuffer of RAW hardware-palette indices (0..31)
 * directly to the LCD, scaled per the current scaling mode. Bypasses the
 * cart's screen palette and screen mode entirely. Used by the pause menu:
 * p8_pause_menu_draw bakes the screen palette into back_page values 0..31
 * (gray-bit becomes index +16), and the menu overlay is drawn with raw hw
 * indices — so we MUST NOT mask & 0x0F here, otherwise the gray-bit info
 * is lost and pixels render in the brighter non-gray version (which is
 * exactly the "boosted colors" symptom on embedded). */
void sys_draw_shell_frame(const uint8_t* framebuffer, int width, int height,
                          const uint32_t* palette) {
    (void)width; (void)height;  /* embedded is always 128x128 */
    pixel_t* lcd = (pixel_t*)lcd_get_active_buffer();

    pico8_scaling_sync();
    const int sw = cur_scale_w, sh = cur_scale_h;
    const int ox = cur_offset_x, oy = cur_offset_y;

    /* RGB565 LUT for the 32 hardware palette entries */
    uint16_t hw565[32];
    for (int i = 0; i < 32; i++) {
        uint32_t rgb = palette[i];
        hw565[i] = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    }

    /* Don't bother caching cleared_bufs here — sys_draw_frame already does
     * it for the gameplay path; the menu only re-blits the same buffer on
     * input changes, so the cost is negligible. The borders stay 0 from
     * the prior gameplay clear. */

    for (int dy = 0; dy < sh; dy++) {
        const uint8_t* src_row = framebuffer + scale_lut_y[dy] * 128;
        pixel_t* dst_row = lcd + (oy + dy) * LCD_WIDTH + ox;
        for (int dx = 0; dx < sw; dx++) {
            /* No mask: 0..31 indexes the full hw palette (16 standard + 16 gray) */
            dst_row[dx] = hw565[src_row[scale_lut_x[dx]] & 0x1F];
        }
    }
}

/* D-pad mouse-emulation toggle (host pause-menu item, see app_main_pico8). */
bool mouse_user_disabled = false;
bool mouse_menu_registered = false;
void mouse_toggle_action(void) {
    mouse_user_disabled = !mouse_user_disabled;
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
 * Audio resampling: 22050 Hz → 48000 Hz + volume scale (fused).
 * ============================================================
 * Linear-interpolation upsampler using a Q16.16 fixed-point accumulator:
 * `step` is computed once per call, inner loop just adds — no per-sample
 * divides (vs old code which did 2 per output sample = ~1600 divides per
 * 60 Hz audio frame). Volume is applied in the same pass to avoid a
 * second linear sweep over the 800-sample DMA buffer.
 *   vol is 0..255 from the retro-go volume table; 255 gives ≈ unity gain.
 */
static void resample_and_scale_audio(const int16_t *src, int src_samples,
                                     int16_t *dst, int dst_samples,
                                     int32_t vol)
{
    uint32_t step = ((uint32_t)src_samples << 16) / (uint32_t)dst_samples;
    uint32_t pos_q = 0;
    int last_idx = src_samples - 1;
    for (int i = 0; i < dst_samples; i++) {
        uint32_t pos  = pos_q >> 16;
        uint32_t frac = (pos_q >> 8) & 0xFF;   /* top 8 bits of fractional */
        int32_t s0 = src[pos];
        int32_t s1 = ((int)pos < last_idx) ? src[pos + 1] : s0;
        int32_t interp = s0 + (((s1 - s0) * (int32_t)frac) >> 8);
        dst[i] = (int16_t)((interp * vol) >> 8);
        pos_q += step;
    }
}

/* Repaint callback for the pause menu overlay */
static void p8_blit(void) {
    /* Clear audio during pause menu repaint (DMA keeps playing otherwise) */
    audio_clear_active_buffer();
    p8_present_display();
    common_ingame_overlay();
}

/* ============================================================
 * app_main_pico8 helpers — extracted from the main loop so each concern
 * lives in one named place. All are static; scope is limited to this TU.
 * ============================================================ */

/* One-shot setup: framework init, cart load, pools, audio, LCD clear. */
static void p8_app_init(void)
{
    ram_start = (uint32_t) &_OVERLAY_PICO8_BSS_END;

    common_emu_state.frame_time_10us = (uint16_t)(100000 / P8_DISPLAY_FPS + 0.5f);
    odroid_system_init(APPID_PICO8, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(NULL, NULL, NULL, NULL, NULL, NULL);

    /* Default scaling to FIT (240×240 centered) on first launch — retro-go's
     * global default is FULL, which stretches PICO-8's 128×128 too far. User
     * can override via the system pause-menu "Scaling" option; the choice
     * persists per-app. Sentinel key "P8ScInit" flags that we've seeded the
     * mode once so we don't keep overwriting the user's later choice. */
    if (odroid_settings_app_int32_get("P8ScInit", 0) == 0) {
        odroid_display_set_scaling_mode(ODROID_DISPLAY_SCALING_FIT);
        odroid_settings_app_int32_set("P8ScInit", 1);
    }

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

    audio_clear_active_buffer();
    audio_clear_inactive_buffer();
    audio_start_playing(AUDIO_SAMPLE_RATE / P8_DISPLAY_FPS);

    lcd_clear_active_buffer();
    lcd_clear_inactive_buffer();
}

/* G&W gamepad state → PICO-8 btn bitmask (A/B swapped for O/X). */
static uint32_t map_gw_to_p8_buttons(const odroid_gamepad_state_t *j)
{
    uint32_t p = 0;
    if (j->values[ODROID_INPUT_LEFT])  p |= (1 << 0);
    if (j->values[ODROID_INPUT_RIGHT]) p |= (1 << 1);
    if (j->values[ODROID_INPUT_UP])    p |= (1 << 2);
    if (j->values[ODROID_INPUT_DOWN])  p |= (1 << 3);
    if (j->values[ODROID_INPUT_A])     p |= (1 << 5);  /* X button (swapped) */
    if (j->values[ODROID_INPUT_B])     p |= (1 << 4);  /* O button (swapped) */
    return p;
}

/* Mouse emulation: D-pad drives cursor, B/A = left/right click, when the
 * cart enables devkit (poke 0x5F2D,1) and actually reads mouse coords. On
 * active mouse mode, strips dpad+face-button bits from *p8_btns so the
 * cart only sees mouse events. Registers a "dpad mouse: on/off" entry in
 * the pause menu so the user can disable it. */
static void update_mouse_emulation(const odroid_gamepad_state_t *j, uint32_t *p8_btns)
{
    static int32_t mx_q8 = 64 << 8;     /* sub-pixel cursor pos, q8 */
    static int32_t my_q8 = 64 << 8;
    static int hold_x = 0;              /* signed frames-held counter */
    static int hold_y = 0;
    static bool mouse_was_active = false;
    static bool click_suppress = false;  /* require A/B release after re-entry */

    bool cart_uses_mouse = (p8.ram[0x5F2D] & 1) && p8.mouse_used;
    /* Re-register every frame so the label reflects the current state
     * (host item label is borrowed by reference). */
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
    if (!mouse_mode) {
        if (mouse_was_active) {
            p8.mouse_btn = 0;
            mouse_was_active = false;
        }
        return;
    }

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
        /* Suppress phantom click from the A/B that closed the pause menu */
        click_suppress = true;
    }

    bool L = j->values[ODROID_INPUT_LEFT];
    bool R = j->values[ODROID_INPUT_RIGHT];
    bool U = j->values[ODROID_INPUT_UP];
    bool D = j->values[ODROID_INPUT_DOWN];

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
     *   capped        4.0 px */
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
    if (mx_q8 > (127 << 8)) mx_q8 = (127 << 8);
    if (my_q8 > (127 << 8)) my_q8 = (127 << 8);

    p8.mouse_x = mx_q8 >> 8;
    p8.mouse_y = my_q8 >> 8;

    /* B = left click (bit 0), A = right click (bit 1) */
    bool a = j->values[ODROID_INPUT_A];
    bool b = j->values[ODROID_INPUT_B];
    if (click_suppress) {
        if (!a && !b) click_suppress = false;
        a = b = false;
    }
    int mb = 0;
    if (b) mb |= 1;
    if (a) mb |= 2;
    p8.mouse_btn = mb;

    /* Strip dpad + face-button bits so the cart only sees mouse */
    *p8_btns &= ~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
                  (1 << 4) | (1 << 5));
}

/* Pause menu: toggle on GAME (Mario) / START (Zelda); when active, drive
 * menu navigation and draw it over the frozen back_page. On close, check
 * whether the user selected "reset cart" and re-init if so. Returns true
 * if the caller should `continue` (menu frame consumed or cart reset). */
static bool handle_pause_menu(const odroid_gamepad_state_t *j, uint32_t p8_btns, int *skip_frame)
{
    static bool prev_pause_btn = false;
    bool pause_now = j->values[ODROID_INPUT_START]  /* GAME (Mario) */
                  || j->values[ODROID_INPUT_X];     /* START (Zelda) */
    if (pause_now && !prev_pause_btn) {
        if (p8_pause_menu_is_active()) p8_pause_menu_close();
        else                           p8_pause_menu_open();
    }
    prev_pause_btn = pause_now;

    if (!p8_pause_menu_is_active()) return false;

    static uint32_t prev_menu = 0;
    static bool menu_suppress = true;
    if (menu_suppress) {
        prev_menu = p8_btns | (1 << 4) | (1 << 5) | (1 << 6);
        menu_suppress = false;
    }
    uint32_t newly = p8_btns & ~prev_menu;
    prev_menu = p8_btns;
    p8_pause_menu_update(newly);

    if (p8_pause_menu_is_active()) {
        p8_pause_menu_draw();
        /* back_page contains raw hw palette indices (0..31) — game pixels
         * pre-mapped through 0x5F10 by p8_pause_menu_draw, menu colors as
         * direct hw indices. sys_draw_shell_frame bypasses screen palette
         * + screen mode and preserves the gray-bit (index +16). Blit to
         * both LCD buffers so the menu is visible regardless of which is
         * currently displaying. */
        audio_clear_active_buffer();
        sys_draw_shell_frame(p8_get_display_buffer(), 128, 128, p8.hardware_palette);
        common_ingame_overlay();
        lcd_swap();
        audio_clear_active_buffer();
        sys_draw_shell_frame(p8_get_display_buffer(), 128, 128, p8.hardware_palette);
        common_ingame_overlay();
        return true;
    }

    /* Menu just closed */
    menu_suppress = true;
    if (p8.next_cart_path[0] != '\0') {
        /* "reset cart" was selected — full re-init from scratch */
        p8.next_cart_path[0] = '\0';
        if (p8.cartdata_dirty) gw_flush_cartdata();
        if (p8.L) { lua_close(p8.L); p8.L = NULL; p8.cart_co = NULL; }
        { extern void luaH_frozen_reset(void); luaH_frozen_reset(); }
        p8_init(ACTIVE_FILE->path, false);
        wdog_refresh();
        p8_setup_coroutine();
        wdog_refresh();
        /* p8_embedded_snapshot_rom already called inside p8_init; don't
         * call again here — would overwrite p8.ram AFTER the Lua state
         * has been rebuilt, potentially into the new Lua heap. */
        *skip_frame = 0;
        strncpy(p8.shell.loaded_cart, ACTIVE_FILE->path, sizeof(p8.shell.loaded_cart) - 1);
    }
    return true;
}

/* Wait for any button press. Used by error/not-found screens. */
static void wait_for_any_button(void)
{
    odroid_gamepad_state_t js;
    do {
        wdog_refresh();
        HAL_Delay(50);
        odroid_input_read_gamepad(&js);
    } while (!js.values[ODROID_INPUT_A] && !js.values[ODROID_INPUT_B] &&
             !js.values[ODROID_INPUT_START]);
}

/* Cart requested a multicart load via _p8_load:<path> error. Returns true
 * on success (caller should `continue`); false on failure. */
static bool handle_multicart_load(const char *cart_path, int *skip_frame)
{
    printf("P8: loading multicart: %s\n", cart_path);

    if (p8.cartdata_dirty) gw_flush_cartdata();

    uint32_t new_size = 0;
    wdog_refresh();
    uint8_t *new_data = odroid_overlay_cache_file_in_flash(cart_path, &new_size, false);
    wdog_refresh();

    if (!new_data || new_size == 0) {
        printf("P8: multicart load failed: %s\n", cart_path);
        return false;
    }

    ROM_DATA = new_data;
    ROM_DATA_LENGTH = new_size;

    /* Save param string before p8_init resets P8_State. The new cart
     * reads it via stat(6) for state transfer. */
    char saved_param[256];
    strncpy(saved_param, p8.next_param_str, sizeof(saved_param) - 1);
    saved_param[sizeof(saved_param) - 1] = '\0';

    /* Reinitialize engine with soft_load=true (matches PICO-8). soft_load
     * preserves RAM above 0x4300 — needed for multicarts sharing data via
     * upper memory (e.g. snekburd's extra spritesheet). */
    p8_init(cart_path, true);
    wdog_refresh();

    strncpy(p8.next_param_str, saved_param, sizeof(p8.next_param_str) - 1);
    p8.next_param_str[sizeof(p8.next_param_str) - 1] = '\0';

    p8_setup_coroutine();
    wdog_refresh();

    *skip_frame = 0;
    p8_embedded_snapshot_rom();
    printf("P8: multicart loaded OK, param='%s'\n", p8.next_param_str);
    return true;
}

/* Show "multicart not found" screen, wait for button, return to retro-go menu. */
__attribute__((noreturn))
static void show_multicart_not_found(const char *cart_path)
{
    lcd_clear_active_buffer();
    odroid_overlay_draw_text(4,  4, LCD_WIDTH - 8, "Multicart not found:", C_GW_YELLOW, C_GW_RED);
    odroid_overlay_draw_text(4, 20, LCD_WIDTH - 8, cart_path,              0xFFFF,      C_GW_RED);
    odroid_overlay_draw_text(4, 44, LCD_WIDTH - 8, "Place cart file in",   C_GW_YELLOW, C_GW_RED);
    odroid_overlay_draw_text(4, 56, LCD_WIDTH - 8, "/roms/pico8/ or",      C_GW_YELLOW, C_GW_RED);
    odroid_overlay_draw_text(4, 68, LCD_WIDTH - 8, "/roms/pico8/.multicarts/", C_GW_YELLOW, C_GW_RED);
    odroid_overlay_draw_text(4, 92, LCD_WIDTH - 8, "Press any button...",  0xFFFF,      C_GW_RED);
    lcd_swap();
    wait_for_any_button();
    odroid_system_switch_app(0);
    while (1) {}  /* unreachable; silences [[noreturn]] warning */
}

/* Show generic error + memory stats, wait for button, return to retro-go menu. */
__attribute__((noreturn))
static void show_error_and_exit(const char *err)
{
    printf("P8 STOPPED: %s\n", err);
    lcd_clear_active_buffer();
    odroid_overlay_draw_text(4,  4, LCD_WIDTH - 8, "PICO-8 Error:", C_GW_YELLOW, C_GW_RED);
    odroid_overlay_draw_text(4, 20, LCD_WIDTH - 8, err,             0xFFFF,      C_GW_RED);
    char mem_info[128];
    snprintf(mem_info, sizeof(mem_info), "pool=%uKB used=%uKB free=%uKB",
             (unsigned)(p8_pool_get_total() / 1024),
             (unsigned)(p8_pool_get_used()  / 1024),
             (unsigned)(p8_pool_get_free()  / 1024));
    odroid_overlay_draw_text(4, 44, LCD_WIDTH - 8, mem_info,              C_GW_YELLOW, C_GW_RED);
    odroid_overlay_draw_text(4, 68, LCD_WIDTH - 8, "Press any button...", 0xFFFF,      C_GW_RED);
    lcd_swap();
    wait_for_any_button();
    if (p8.cartdata_dirty) gw_flush_cartdata();
    odroid_system_switch_app(0);
    while (1) {}  /* unreachable */
}

/* Fill one display frame's worth of audio. Bresenham: 22050/60 = 367.5
 * samples/frame — accumulator pushes to 368 every other frame. */
static void fill_audio_for_frame(int audio_out_samples)
{
    if (common_emu_sound_loop_is_muted()) return;

    static int audio_frac = 0;
    int p8_samples = P8_AUDIO_RATE / P8_DISPLAY_FPS;   /* 367 */
    audio_frac += P8_AUDIO_RATE % P8_DISPLAY_FPS;      /* +30 each frame */
    if (audio_frac >= P8_DISPLAY_FPS) {
        audio_frac -= P8_DISPLAY_FPS;
        p8_samples++;                                   /* 368 this frame */
    }
    p8_fill_audio(p8_audio_buf, p8_samples);
    int32_t vol = common_emu_sound_get_volume();
    int16_t *audio_out = audio_get_active_buffer();
    resample_and_scale_audio(p8_audio_buf, p8_samples,
                             audio_out, audio_out_samples, vol);
}

/* Once-per-second perf + memory line. */
static void log_perf_line(int frame_num, uint32_t vm_ms, uint32_t dr_ms, uint32_t tot_ms)
{
    printf("F%d vm=%lu dr=%lu tot=%lu | mem=%u/%uKB free=%uKB",
           frame_num,
           (unsigned long)vm_ms,
           (unsigned long)dr_ms,
           (unsigned long)tot_ms,
           (unsigned)(p8_pool_get_used()  / 1024),
           (unsigned)(p8_pool_get_total() / 1024),
           (unsigned)(p8_pool_get_free()  / 1024));
    if (p8.L) {
        int gc_kb = lua_gc(p8.L, LUA_GCCOUNT,  0);
        int gc_b  = lua_gc(p8.L, LUA_GCCOUNTB, 0);
        printf(" gc=%d.%dKB base=%ld", gc_kb, gc_b / 100, (long)p8.base_fps);
    }
    printf("\n");
}

/* ============================================================
 * Main entry point
 * ============================================================ */
void app_main_pico8(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    (void)load_state; (void)start_paused; (void)save_slot;

    p8_app_init();

    odroid_dialog_choice_t options[] = { ODROID_DIALOG_CHOICE_LAST };
    odroid_gamepad_state_t joystick;
    const int audio_out_samples = AUDIO_SAMPLE_RATE / P8_DISPLAY_FPS;
    int frame_num = 0;
    int skip_frame = 0;  /* For 30fps carts: skip VM on odd display frames */

    /* Main loop — always 60fps display. For 30fps carts, p8_step_frame
     * runs every other frame; the alternate frame re-displays the last output. */
    while (true) {
        wdog_refresh();
        uint32_t t0 = HAL_GetTick();

        common_emu_frame_loop();
        /* Override retro-go's frame-integrator decisions. With variable PICO-8
         * frame costs (6ms normal, 130ms+ death animation), honouring the
         * integrator's skip_frames=1/2 advice causes permanent slowdown —
         * visible framerate never recovers because we'd keep skipping while
         * the cart logic runs normally. Zero them so common_emu_sound_sync()
         * below always does a single fixed DMA-tick wait. */
        common_emu_state.skip_frames = 0;
        common_emu_state.pause_frames = 0;

        /* Read input through framework (handles retro-go system menu, turbo). */
        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &p8_blit);
        common_emu_input_loop_handle_turbo(&joystick);

        uint32_t p8_btns = map_gw_to_p8_buttons(&joystick);
        update_mouse_emulation(&joystick, &p8_btns);
        p8_set_button_state(p8_btns);

        if (handle_pause_menu(&joystick, p8_btns, &skip_frame)) continue;

        /* Fill audio BEFORE the VM step. Audio is always the tightest
         * real-time deadline: the DMA half-buffer runs out every 16.67ms
         * and must be refilled before then or we get crackle (last half
         * replayed). Filling here — right after the previous iteration's
         * sound_sync wait — gives the DMA the maximum possible headroom
         * before a slow VM frame (death animation, heavy _update) pushes
         * the next fill past the deadline. There's a ~1-frame (16.67ms)
         * latency cost (sfx() triggered in this frame's _update is heard
         * in the next frame), which is well below audible threshold. */
        fill_audio_for_frame(audio_out_samples);

        /* 30fps carts run VM every other display frame; the alternate frame
         * re-displays the last output. 60fps carts run VM every frame. */
        bool run_vm = true;
        if (p8.target_fps <= 30) {
            skip_frame ^= 1;
            run_vm = !skip_frame;
        }

        uint32_t t1 = HAL_GetTick();
        if (run_vm && !p8_step_frame()) {
            const char *err = p8_get_last_error();
            /* Multicart load request uses a synthetic "_p8_load:<path>" error */
            if (strncmp(err, "_p8_load:", 9) == 0) {
                const char *cart_path = err + 9;
                if (handle_multicart_load(cart_path, &skip_frame)) continue;
                show_multicart_not_found(cart_path);  /* noreturn */
            }
            show_error_and_exit(err);  /* noreturn */
        }

        /* Display — host-driven tick (re-displays last frame on skip frames) */
        uint32_t t3 = HAL_GetTick();
        p8_display_tick();
        common_ingame_overlay();
        lcd_swap();
        uint32_t t4 = HAL_GetTick();

        /* Periodic tasks: every 60 frames (~1 sec at 60fps) */
        if (++frame_num % 60 == 0) {
            if (p8.cartdata_dirty) gw_flush_cartdata();  /* debounced flush */
            log_perf_line(frame_num, t3 - t1, t4 - t3, t4 - t0);
        }

        /* Stop-the-world GC: full collect between frames. Incremental
         * GCSTEP caused crashes (bubble_bobble, pico_ball) — restart/stop
         * cycle corrupts GC state with paged allocator. */
        if (p8.L) lua_gc(p8.L, LUA_GCCOLLECT, 0);

        /* Nothing between here and the next iteration's top-of-loop reset
         * reads skip/pause, so we don't re-zero them here. */
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
 * in main pool (AXI SRAM). Only accessed by reload() which is
 * rare, so doesn't need fast memory. Frees ITCM for hot code.
 * ============================================================ */
/* embedded_cart_rom declared earlier (before p8_itcm_init) */

/* Called after successful cart load to snapshot p8.ram[0..0x42FF].
 * Allocates from main pool (AXI SRAM) — reload() is rare. */
void p8_embedded_snapshot_rom(void) {
    if (!embedded_cart_rom) {
        embedded_cart_rom = (uint8_t*)p8_pool_malloc(P8_ROM_SIZE);
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
