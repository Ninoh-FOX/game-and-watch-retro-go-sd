# PICO-8 External Module Architecture

## Overview

The PICO-8 engine runs as a **separately-distributed binary module** on the
Game & Watch Retro-Go SD firmware. The GPL firmware ships with a stub that
displays an install prompt; users replace it with the real engine binaries
downloaded from a separate distribution.

This design keeps the GPL firmware free of proprietary engine code while
allowing the engine to call firmware functions (LCD, audio, input, libc)
through a **versioned ABI table** at a fixed flash offset.

```
┌─────────────────────────────────────────────────────────────┐
│                    STM32H7B0 Memory Map                     │
├──────────────┬──────────────────────────────────────────────┤
│ Internal     │ GPL Firmware (always resident)               │
│ Flash        │ ┌──────────────────────────────────────────┐ │
│ (256KB)      │ │ ISR Vector Table (684 bytes)             │ │
│              │ ├──────────────────────────────────────────┤ │
│              │ │ ★ Firmware ABI Table (VTOR+0x400)        │ │
│              │ │   gw_firmware_abi_t: ~500 bytes           │ │
│              │ │   120+ function pointers + data pointers  │ │
│              │ ├──────────────────────────────────────────┤ │
│              │ │ .text (firmware code, libc, HAL)         │ │
│              │ │ .rodata, .data                           │ │
│              │ └──────────────────────────────────────────┘ │
├──────────────┼──────────────────────────────────────────────┤
│ ITCM         │ Hot code (loaded from pico8_itcm.bin)       │
│ (64KB)       │ ┌──────────────────────────────────────────┐ │
│ @ 0x00000000 │ │ lvm.o  (VM dispatch)        ~16KB       │ │
│              │ │ ltable.o (hash lookups)       ~6KB       │ │
│              │ │ lgc.o  (garbage collector)    ~7KB       │ │
│              │ │ lapi.o (Lua C API)            ~8KB       │ │
│              │ │ ldo.o  (call/yield/pcall)     ~3KB       │ │
│              │ │ p8_render.o (spr/map/tline)   ~6KB       │ │
│              │ │ veneers + padding             ~1KB       │ │
│              │ ├──────────────────────────────────────────┤ │
│              │ │ back_page (8bpp framebuffer)  16KB       │ │
│              │ │ free                          ~2KB       │ │
│              │ └──────────────────────────────────────────┘ │
├──────────────┼──────────────────────────────────────────────┤
│ DTCM         │ Firmware BSS + PICO-8 RAM                   │
│ (128KB)      │ ┌──────────────────────────────────────────┐ │
│ @ 0x20000000 │ │ .data + .bss (firmware)      ~14KB      │ │
│              │ │ ._dtcm_p8ram (p8.ram)         64KB      │ │
│              │ │ heap (malloc)                  22KB      │ │
│              │ │ stack                          24KB      │ │
│              │ └──────────────────────────────────────────┘ │
├──────────────┼──────────────────────────────────────────────┤
│ AXI SRAM     │ Overlay + Pools                              │
│ (1MB)        │ ┌──────────────────────────────────────────┐ │
│ @ 0x24000000 │ │ LCD framebuffers (uncached)   300KB      │ │
│              │ ├──────────────────────────────────────────┤ │
│              │ │ ★ pico8.bin overlay           ~125KB     │ │
│              │ │   .pico8_entry (trampoline)              │ │
│              │ │   main_pico8.o code                      │ │
│              │ │   engine code + bridge trampolines       │ │
│              │ │   .data sections                         │ │
│              │ ├──────────────────────────────────────────┤ │
│              │ │ Overlay BSS (zeroed at load)  ~40KB      │ │
│              │ ├──────────────────────────────────────────┤ │
│              │ │ Main TLSF pool (Lua heap)    ~717KB      │ │
│              │ └──────────────────────────────────────────┘ │
├──────────────┼──────────────────────────────────────────────┤
│ QSPI Flash   │ XIP code (loaded from pico8.ro)              │
│ (32MB)       │ ┌──────────────────────────────────────────┐ │
│ @ 0x90000000 │ │ pico8.ro (cold code, sentinel-patched)   │ │
│              │ │   Lua compiler, cart loader, savestate    │ │
│              │ │   ~92KB                                   │ │
│              │ └──────────────────────────────────────────┘ │
├──────────────┼──────────────────────────────────────────────┤
│ AHB SRAM     │ AHB pool (124KB) — large Lua allocations    │
│ SRD SRAM     │ SRD pool (32KB) — overflow allocations      │
└──────────────┴──────────────────────────────────────────────┘
```

## The Three Engine Binaries

The engine is distributed as three files placed in `/cores/` on the SD card:

| File | Size | Contents | Loaded to |
|------|------|----------|-----------|
| `pico8.bin` | ~125KB | Overlay: engine code + bridge trampolines + data | AXI SRAM (RAM_EMU) |
| `pico8.ro` | ~92KB | Cold code: Lua compiler, cart loader, savestate | QSPI flash (XIP) |
| `pico8_itcm.bin` | ~47KB | Hot code: VM dispatch, GC, table ops, renderer | ITCM RAM |

## Launch Sequence

```
User selects a .p8 cart in the retro-go launcher
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 1. CACHE pico8.ro TO FLASH   │
    │                               │
    │ Pico8CacheCodeToFlash():      │
    │ • Read /cores/pico8.ro → RAM  │
    │ • Scan for 0xBEEFxxxx addrs   │
    │ • Patch to actual QSPI addr   │
    │ • Reprogram flash             │
    │ (idempotent — skips if        │
    │  already patched from prior   │
    │  boot)                        │
    └───────────────┬───────────────┘
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 2. LOAD pico8.bin TO RAM     │
    │                               │
    │ odroid_overlay_cache_file_    │
    │ in_ram("/cores/pico8.bin",   │
    │        __RAM_EMU_START__)     │
    │                               │
    │ → 125KB loaded to 0x2404b000 │
    └───────────────┬───────────────┘
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 3. ZERO BSS                  │
    │                               │
    │ memset(loaded_end, 0, 256KB)  │
    │ Engine BSS (~40KB) zeroed.   │
    │ Uses actual loaded size, not  │
    │ linker-defined stub size.     │
    └───────────────┬───────────────┘
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 4. PATCH RAM OVERLAY         │
    │                               │
    │ PatchPico8Region():           │
    │ Scan loaded data for          │
    │ 0xBEEFxxxx sentinel addrs.   │
    │ Replace with actual QSPI     │
    │ addresses (from step 1).     │
    │ Only scans loaded data, NOT   │
    │ zeroed BSS (avoids false     │
    │ positives).                   │
    │                               │
    │ → 16 refs patched            │
    └───────────────┬───────────────┘
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 5. CACHE MAINTENANCE         │
    │                               │
    │ SCB_CleanDCache_by_Addr()    │
    │ SCB_InvalidateICache()       │
    │                               │
    │ Ensures CPU fetches patched   │
    │ code, not stale cache.        │
    └───────────────┬───────────────┘
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 6. DISPATCH VIA TRAMPOLINE   │
    │                               │
    │ Jump to __RAM_EMU_START__ | 1 │
    │ (offset 0 of overlay = entry │
    │  trampoline, NOT the linker  │
    │  veneer for app_main_pico8)  │
    │                               │
    │ .pico8_entry section:         │
    │   b.w app_main_pico8         │
    └───────────────┬───────────────┘
                    │
                    ▼
    ┌───────────────────────────────┐
    │ 7. ENGINE INIT               │
    │                               │
    │ app_main_pico8():             │
    │ a) p8_firmware_bridge_init() │
    │    Read ABI data pointers    │
    │    (ACTIVE_FILE, ROM_DATA,   │
    │     ram_start, _impure_ptr)  │
    │                               │
    │ b) ram_start = BSS_END       │
    │    p8_firmware_bridge_sync() │
    │    Write ram_start back to   │
    │    firmware global via ABI   │
    │                               │
    │ c) odroid_system_init() ─┐   │
    │    (via ABI trampoline)  │   │
    │                          │   │
    │ d) p8_init():            │   │
    │    • DTCM p8.ram from    │
│      platform struct     │   │
    │    • Pool allocator init │   │
    │    • ITCM hot code load  │   │
    │      from SD card        │   │
    │    • Sentinel-patch ITCM │   │
    │    • back_page alloc     │   │
    │      (AFTER hot code!)   │   │
    │    • Cart extract        │   │
    │    • Lua VM init         │   │
    │    • openlibs            │   │
    │    • Cart compile + run  │   │
    └───────────────────────────────┘
```

## Firmware ABI Table

The firmware publishes a `gw_firmware_abi_t` struct at a **fixed offset**
from the start of internal flash (`ORIGIN(FLASH) + 0x400`). The engine
discovers the absolute address at runtime by reading the ARM VTOR register:

```c
static inline const gw_firmware_abi_t *gw_firmware_abi(void) {
    uintptr_t base = *(const volatile uint32_t *)0xE000ED08;  // VTOR
    return (const gw_firmware_abi_t *)(base + 0x400);
}
```

This works regardless of which flash bank the firmware runs from
(bank 1 @ 0x08000000 or bank 2 @ 0x08100000).

### Struct layout (append-only, versioned)

```c
typedef struct {
    uint32_t version;    // GW_FIRMWARE_ABI_VERSION (bump on breaking change)
    uint32_t size;       // sizeof(gw_firmware_abi_t)

    // libc: string.h (17 functions)
    void *(*memchr)(...); void *(*memcpy)(...); ...

    // libc: ctype.h (11 functions)
    int (*isalnum)(int); ...

    // libc: stdlib/stdio/time/setjmp/locale/math
    void (*abort)(void); FILE *(*fopen)(...); ...
    int (*vfprintf)(...); int (*vsnprintf)(...); ...  // va_list versions
    int (*setjmp)(jmp_buf); void (*longjmp)(jmp_buf, int);

    // libgcc helpers
    int64_t (*aeabi_d2lz)(double); ...
    int64_t (*ldivmod_quot)(int64_t, int64_t); ...

    // FatFs
    FRESULT (*f_opendir)(...); ...

    // G&W hardware: LCD, audio, allocators, RTC, watchdog, HAL
    void (*lcd_swap)(void); void *(*lcd_get_active_buffer)(void); ...
    void (*HAL_Delay)(uint32_t); uint32_t (*HAL_GetTick)(void);

    // retro-go framework
    void (*odroid_system_init)(int, int); ...
    void (*odroid_input_read_gamepad)(...); ...
    bool (*common_emu_frame_loop)(void); ...

    // Firmware data pointers (engine reads shared globals through these)
    void **ROM_DATA_ptr;           // &ROM_DATA
    void **ACTIVE_FILE_ptr;        // &ACTIVE_FILE
    uint32_t *ram_start_ptr;       // &ram_start
    void **impure_ptr_ptr;         // &_impure_ptr (for errno/stdio macros)
    void *dtcm_p8ram_start;        // &__dtcm_p8ram_start__
} gw_firmware_abi_t;
```

**Rules:**
- NEVER reorder or remove fields (breaking change → bump version)
- Only APPEND new fields at the end
- Engine checks `version` and `size` at init

### Files

| File | Repo | Purpose |
|------|------|---------|
| `Core/Inc/retro-go/gw_firmware_abi.h` | GPL | Struct definition + VTOR accessor |
| `Core/Src/retro-go/gw_firmware_abi.c` | GPL | Populated instance (function addresses) |
| `Core/Src/porting/pico8/p8_firmware_bridge.cpp` | Engine overlay | ABI trampolines (libc + G&W) |

### Linker placement

Both `STM32H7B0VBTx_SDCARD.ld` and `STM32H7B0VBTx_FLASH.ld`:
```
.firmware_abi ORIGIN(FLASH) + 0x400 :
{
    KEEP(*(.firmware_abi))
} >FLASH
```

The `Makefile` must include `-j .firmware_abi` in the `objcopy` command that
creates `intflash.bin`, otherwise the section is omitted from the binary.

## Symbol Renaming (objcopy --redefine-syms)

The engine binary must NOT contain direct references to firmware symbol
addresses — those change between firmware builds. Instead, every external
symbol is **renamed** at the object-file level using `objcopy --redefine-syms`:

```
# pico8_abi_redefine_syms.txt
# Format: OLD_NAME NEW_NAME
memcpy p8_memcpy
printf p8_printf
lcd_swap p8_lcd_swap
odroid_input_read_gamepad p8_odroid_input_read_gamepad
ACTIVE_FILE p8_ACTIVE_FILE
...
```

**Build flow:**

```
 Engine .c/.cpp source
        │
        ▼
 arm-none-eabi-g++ -c  →  build/pico8/foo.o  (has "memcpy" symbol)
        │
        ▼
 arm-none-eabi-objcopy --redefine-syms=pico8_abi_redefine_syms.txt
        │
        ▼
 build/pico8/foo.o  (now has "p8_memcpy" symbol)
        │
        ▼
 Linker resolves "p8_memcpy" → bridge trampoline in overlay
```

### What gets renamed

| Category | Count | Examples |
|----------|-------|---------|
| libc string/ctype | ~28 | memcpy, strlen, isalpha, tolower |
| libc stdio | ~20 | fopen, printf, fread, fflush |
| libc stdlib/math | ~8 | abort, strtod, pow, qsort |
| libc misc | ~5 | setjmp, longjmp, time, localeconv |
| libgcc helpers | ~4 | __aeabi_d2lz, __aeabi_ldivmod |
| FatFs | 3 | f_opendir, f_closedir, f_readdir |
| G&W hardware | ~20 | lcd_swap, audio_start_playing, wdog_refresh |
| retro-go framework | ~20 | odroid_system_init, common_emu_frame_loop |
| Firmware data | ~7 | ACTIVE_FILE, ROM_DATA, ram_start |
| **Total** | **~115** | |

### What is NOT renamed (by design)

- **Engine-internal symbols** (p8_*, lua*, gcpage_*): resolved within overlay
- **Linker symbols** (__RAM_EMU_START__, __ahbram_end__): same in both builds
- **common_emu_state**: struct with `.field` access — can't transparently redirect through a pointer. Same BSS address in both builds (verified).

## Bridge Trampolines

Each renamed symbol has a **trampoline** in `p8_firmware_bridge.cpp`:

```c
// Function trampoline — reads ABI, indirect-calls firmware function
void *p8_memcpy(void *d, const void *s, size_t n) {
    return gw_firmware_abi()->memcpy(d, s, n);
}

// Variadic trampoline — converts to va_list form
int p8_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = gw_firmware_abi()->vprintf(fmt, ap);
    va_end(ap);
    return r;
}

// Data variable — initialized from ABI at engine init
void *p8_ACTIVE_FILE = 0;
// Set by p8_firmware_bridge_init():
//   p8_ACTIVE_FILE = *gw_firmware_abi()->ACTIVE_FILE_ptr;
```

### Special cases

**setjmp/longjmp**: Cannot be trampolined (wrapper would save the wrong
stack frame). Provided as self-contained ARM Thumb assembly in the bridge:
```asm
p8_setjmp:
    mov    r1, r0
    stmia  r1!, {r4-r11}     // save callee-saved regs
    mov    r2, sp
    mov    r3, lr
    stmia  r1!, {r2, r3}     // save sp + lr
    movs   r0, #0            // return 0
    bx     lr
```

**free/realloc**: NOT in the bridge — the engine provides its own pool
allocator (`p8_pool_realloc`, `p8_pool_free`). After rename, the engine's
custom `free`→`p8_free` and `realloc`→`p8_realloc` resolve to the pool.

**__aeabi_ldivmod**: Returns a {quot, rem} pair in r0-r3 per AAPCS. Cannot
be expressed as a C function pointer. Implemented as a naked asm trampoline
that calls `ldivmod_quot` and `ldivmod_rem` from the ABI separately and
repacks the result.

## Sentinel Patching

The engine is linked at a **sentinel virtual address** (0xBEEF0000) for its
cold code (`.pico8_code` section). At runtime, the cold code is cached to
QSPI flash at whatever address the flash allocator provides. The difference
(actual - sentinel) is applied to all sentinel-range values in:

1. **pico8.ro** (flash blob): 765 refs patched, then reprogrammed to flash
2. **pico8.bin** (RAM overlay): 16 refs patched (function pointers to cold code)
3. **pico8_itcm.bin** (ITCM): 5 refs patched (veneers to cold code)

```
Sentinel base:  0xBEEF0000
Code size:      ~92KB
Sentinel range: 0xBEEF0000 .. 0xBF006xxx

Patch formula:  new_value = old_value + (actual_qspi_addr - 0xBEEF0000)
```

**Important:** The RAM overlay scan covers ONLY the loaded pico8.bin data,
NOT the zeroed BSS region. BSS is all zeros (no sentinel matches), and
scanning data regions risks false positives if any fix32 constant falls
in the -16657..-16565 range (0xBEEFxxxx as 16.16 fixed-point).

## Entry Point

The firmware cannot call `app_main_pico8()` directly — the linker veneer
would target the **stub's** entry point offset, not the engine's. Instead:

1. Both stub and engine place a `.pico8_entry` section at overlay offset 0
2. The section contains a single `b.w app_main_pico8` instruction
3. Firmware dispatches via function pointer: `((void(*)(...))(__RAM_EMU_START__|1))(...)`

```
Linker script:
    .overlay_pico8 __RAM_EMU_START__ : {
        KEEP(*(.pico8_entry))    ← always at offset 0
        build/pico8/main_pico8.o (...)
        build/pico8/*.o (...)
    }
```

## ITCM Back-Page Allocation

**Critical:** After loading ITCM hot code, the ITCM bump allocator
(`itc_init` / `itc_malloc`) must be advanced past the loaded code BEFORE
allocating `back_page`. The firmware's `itc_init()` resets the allocator
to `__itcram_end__`, which in the GPL firmware is 0 (empty ITCM section).

```c
itc_init();                        // resets allocator to __itcram_end__ (= 0 on GPL)
itc_malloc(itcm_loaded_size);      // advance past loaded hot code
void *bp = itc_calloc(1, 16384);   // allocate back_page AFTER hot code
```

Without the unconditional advance, `itc_calloc` returns 0x00000000 and
`back_page` overlaps with the VM hot code — causing progressive corruption
of Lua VM instructions during rendering.

## Platform Struct (`p8_platform_t`)

The engine receives all host-specific services through a `p8_platform_t`
struct passed at init. This includes memory regions (main pool, AHB, SRD),
special memory (DTCM for p8.ram, ITCM allocator, scratch buffer for
savestate staging), timing callbacks, and platform init hooks.

The engine source code contains **no references** to G&W firmware functions
or headers — all platform coupling is through this struct. The struct is
defined in `external/pico8-engine/include/p8_platform.h`.

## DTCM p8.ram Allocation

The engine's 64KB PICO-8 RAM (`p8.ram`) is placed in DTCM for zero-wait
data access. The GPL firmware reserves space via a linker section:

```
._dtcm_p8ram (NOLOAD) : {
    __dtcm_p8ram_start__ = .;
    . = . + 0x10000;
    __dtcm_p8ram_end__ = .;
} >DTCMRAM
```

The engine reads the DTCM address from the ABI (`dtcm_p8ram_start` field)
rather than using the baked linker symbol, because BSS layout differences
between GPL and closed builds would place `__dtcm_p8ram_start__` at
different addresses.

Heap size was reduced from 32KB to 22KB to fit the 64KB reservation.

## Maintenance Checklist

### When adding a new firmware function the engine needs to call:

1. Add field to `gw_firmware_abi_t` (APPEND at end, both headers)
2. Add initializer to `gw_firmware_abi.c` (GPL repo)
3. Add rename entry to `pico8_abi_redefine_syms.txt`
4. Add bridge trampoline to `p8_firmware_bridge.cpp`
5. Bump `GW_FIRMWARE_ABI_VERSION` if removing/reordering (don't do this)

### When the firmware adds/removes globals the engine reads:

1. Add data-pointer field to `gw_firmware_abi_t`
2. Add rename entry
3. Add bridge variable + init in `p8_firmware_bridge_init()`
4. For write-back globals: add to `p8_firmware_bridge_sync()`

### When updating the engine:

1. Rebuild with `make clean && make docker`
2. Copy all 3 files (`pico8.bin`, `pico8.ro`, `pico8_itcm.bin`) to SD
3. GPL firmware does NOT need reflashing (unless ABI version changed)

### When updating the GPL firmware:

1. If only non-pico8 code changed: just reflash. Engine binaries on SD
   continue to work (ABI stable).
2. If ABI struct changed (new fields appended): engine still works
   (reads only fields it knows about, checks `version` and `size`).
3. If ABI struct reordered/removed: BREAKING. Bump version. Engine
   must be rebuilt.

## Performance Notes

- Bridge trampoline overhead: ~6 cycles per call (load ABI pointer + indirect branch)
- Hot-path functions (memcpy, memset, lcd_swap): called at frame granularity, overhead negligible
- Per-pixel/per-sample operations stay within the engine overlay and ITCM — no ABI calls
- Pool allocator overhead: ~4KB of overlay space for bridge + platform code (721→717KB pool)
