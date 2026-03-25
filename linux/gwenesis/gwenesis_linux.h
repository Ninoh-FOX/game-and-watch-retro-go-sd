#pragma once

#include <stddef.h>

#include "rg_emulators.h"

#ifdef TARGET_GNW
/* Filled in main_gwenesis before load_cartridge() (rom_manager.h omits these under LINUX_EMU). */
extern unsigned int ROM_DATA_LENGTH;
#endif

extern retro_emulator_file_t linux_active_file;

/* Set by main.c before app_main_gwenesis() */
extern unsigned char *gwenesis_linux_rom;
extern size_t gwenesis_linux_rom_size;
/* Full path to ROM file (for SRAM filename) */
extern char gwenesis_linux_rom_path[512];
