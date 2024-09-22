#include <odroid_system.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "gw_linker.h"
#include "gw_malloc.h"
#include "rg_emulators.h"
#include "rg_i18n.h"
#include "bitmaps.h"
#include "gui.h"
#include "rom_manager.h"
#include "gw_lcd.h"
#include "main.h"
#include "main_gb.h"
#include "main_gb_tgbdual.h"
#include "main_nes.h"
#include "main_nes_fceu.h"
#include "main_smsplusgx.h"
#include "main_pce.h"
#include "main_msx.h"
#include "main_gw.h"
#include "main_wsv.h"
#include "main_gwenesis.h"
#include "main_a7800.h"
#include "main_amstrad.h"
#include "main_zelda3.h"
#include "main_smw.h"
#include "main_videopac.h"
#include "main_celeste.h"
#include "main_tama.h"
#include "main_a2600.h"
#include "rg_rtc.h"
#include "heap.hpp"
#include "ff.h"
#include "gw_flash.h"


const unsigned char *ROM_DATA = NULL;
unsigned ROM_DATA_LENGTH;
const char *ROM_EXT = NULL;
retro_emulator_file_t *ACTIVE_FILE = NULL;

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */
// Increase when adding new emulators
#define MAX_EMULATORS 19
static retro_emulator_t emulators[MAX_EMULATORS];
static rom_system_t systems[MAX_EMULATORS];
static int emulators_count = 0;

static retro_emulator_file_t *CHOSEN_FILE = NULL;

/* Copy file into flash "cache" section */
static const uint8_t *copy_file_to_cache(char *file_path, uint32_t size, bool byte_swap) {
    FIL file;
    FRESULT fr;
    UINT bytes_read;

    uint32_t address_in_flash = (&__CACHEFLASH_START__ - &__EXTFLASH_BASE__);
    uint8_t *address_in_mem = (&__CACHEFLASH_START__);
    uint8_t buffer[512];
    uint32_t offset = 0;
    bool flash_needed = false;

    printf("file_path %s %ld\n",file_path,size/1024);

    // Check file content will fit in flash
    assert((&__CACHEFLASH_END__ - &__CACHEFLASH_START__) >= size);

    fr = f_open(&file, file_path, FA_READ);
    if (fr != FR_OK) {
        return NULL;
    }

    // Check if content to load is different from cache content
    while (offset < size) {
        fr = f_read(&file, buffer, sizeof(buffer), &bytes_read);
        if (fr != FR_OK || bytes_read == 0) {
            break;
        }
        if (byte_swap) {
            for (int i=0; i < sizeof(buffer); i+=2)
            {
                char temp = buffer[i];
                buffer[i]=buffer[i+1];
                buffer[i+1]=temp;
            }
        }
        if (memcmp(buffer, (const void *)(address_in_mem + offset), bytes_read) != 0) {
            flash_needed = true;
            break;
        }
        offset += bytes_read;
    }

    if (flash_needed) {
        f_lseek(&file, 0);
        offset = 0;

        OSPI_DisableMemoryMappedMode();

        // Erase flash memory
        OSPI_EraseSync(address_in_flash, size);

        // read file and write data in flash
        while (offset < size) {
            wdog_refresh();

            fr = f_read(&file, buffer, sizeof(buffer), &bytes_read);
            if (fr != FR_OK || bytes_read == 0) {
                break;
            }
            if (byte_swap) {
                for (int i=0; i < sizeof(buffer); i+=2)
                {
                    char temp = buffer[i];
                    buffer[i]=buffer[i+1];
                    buffer[i+1]=temp;
                }
            }
            OSPI_Program(address_in_flash + offset, buffer, bytes_read);
            offset += bytes_read;
        }

        OSPI_EnableMemoryMappedMode();
    }

    f_close(&file);

    return address_in_mem;
}

/* copy file content into ram */
static int copy_file_to_ram(char *file_path, char *ram_dest) {
    FIL file;
    FRESULT fr;
    UINT bytes_read;

    fr = f_open(&file, file_path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }

    uint32_t total_written = 0;

    while (f_read(&file, ram_dest+total_written, 32*1024, &bytes_read) == FR_OK) {
        wdog_refresh();

        if (bytes_read == 0) {
            break;
        }

        total_written += bytes_read;
    }

    f_close(&file);

    return true;
}

static void event_handler(gui_event_t event, tab_t *tab)
{
    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    listbox_item_t *item = gui_get_selected_item(tab);
    retro_emulator_file_t *file = (retro_emulator_file_t *)(item ? item->arg : NULL);

    if (event == TAB_INIT)
    {
        emulator_init(emu);

        if (emu->roms.count > 0)
        {
            sprintf(tab->status, "%s", emu->system_name);
            gui_resize_list(tab, emu->roms.count);

            for (int i = 0; i < emu->roms.count; i++)
            {
                tab->listbox.items[i].text = emu->roms.files[i].name;
                tab->listbox.items[i].arg = (void *)&emu->roms.files[i];
            }

            gui_sort_list(tab, 0);
            tab->is_empty = false;
        }
        else
        {
            sprintf(tab->status, " No games");
            gui_resize_list(tab, 8);
            //size_t len = 0;
            //tab->listbox.items[0].text = asnprintf(NULL, &len, "Place roms in folder: /roms/%s", emu->dirname);
            //len = 0;
            //tab->listbox.items[2].text = asnprintf(NULL, &len, "With file extension: .%s", emu->ext);
            //tab->listbox.items[4].text = "Use SELECT and START to navigate.";
            tab->listbox.cursor = 3;
            tab->is_empty = true;
        }
    }

    /* The rest of the events require a file to be selected */
    if (file == NULL)
        return;

    if (event == KEY_PRESS_A)
    {
        emulator_show_file_menu(file);
    }
    else if (event == KEY_PRESS_B)
    {
        emulator_show_file_info(file);
    }
    else if (event == TAB_IDLE)
    {
    }
    else if (event == TAB_REDRAW)
    {
    }
}

static void add_emulator(const char *system, const char *dirname, const char* ext,
                         const void *logo, const void *header, game_data_type_t game_data_type)
{
    assert(emulators_count <= MAX_EMULATORS);
    retro_emulator_t *p = &emulators[emulators_count];
    rom_system_t *s = &systems[emulators_count];
    emulators_count++;

    strcpy(p->system_name, system);
    strcpy(p->dirname, dirname);
    snprintf(p->exts, sizeof(p->exts), " %s ", ext);
    p->roms.count = 0;
    p->roms.maxcount = 100;
    p->roms.files = ram_calloc(p->roms.maxcount, sizeof(retro_emulator_file_t)); // TODO SD : improve this
    p->initialized = false;
    p->system = s;

    s->extension = (char *)ext;
    s->roms = p->roms.files;
    s->roms_count = p->roms.count;
    s->system_name = (char *)system;
    s->game_data_type = game_data_type;

    gui_add_tab(dirname, logo, header, p, event_handler);

    emulator_init(p);
}

static int scan_folder_cb(const rg_scandir_t *entry, void *arg)
{
    retro_emulator_t *emu = (retro_emulator_t *)arg;
    const char *ext = rg_extension(entry->basename);
    uint8_t is_valid = false;
    char ext_buf[32];

    // Skip hidden files
    if (entry->basename[0] == '.')
        return RG_SCANDIR_SKIP;

    printf("name = %s\n",entry->basename);
    if (entry->is_file && ext[0])
    {
        snprintf(ext_buf, sizeof(ext_buf), " %s ", ext);
        is_valid = strstr(emu->exts, rg_strtolower(ext_buf)) != NULL;
        printf("%d - ext_buf = '%s' ext='%s'\n",is_valid,ext_buf,emu->exts);
    }
    else if (entry->is_dir)
    {
        printf("Found subdirectory '%s'", entry->path);
        is_valid = true;
    }

    if (!is_valid)
        return RG_SCANDIR_CONTINUE;

    if (emu->roms.count + 1 > emu->roms.maxcount)
    {
        return RG_SCANDIR_STOP;
    }

    emu->roms.files[emu->roms.count++] = (retro_emulator_file_t) {
        .name = strdup(entry->basename),
        .ext = ext,
        .address = 0,
        .path = (char *)const_string(entry->path),
        .size = entry->size,
        .system = emu->system,
        .region = REGION_NTSC,
    };
    emu->system->roms_count = emu->roms.count;

    printf("emu->roms.count %d\n",emu->roms.count);
    return RG_SCANDIR_CONTINUE;
}

void emulator_init(retro_emulator_t *emu)
{
    char folder[RG_PATH_MAX];

    if (emu->initialized)
        return;

    emu->initialized = true;

    printf("Retro-Go: Initializing emulator '%s'\n", emu->system_name);

    sprintf(folder, ODROID_BASE_PATH_SAVES "/%s", emu->dirname);
    rg_storage_mkdir(folder);

    sprintf(folder, ODROID_BASE_PATH_ROMS "/%s", emu->dirname);
    rg_storage_mkdir(folder);

    snprintf(folder, sizeof(folder), "%s/%s", RG_BASE_PATH_ROMS, emu->dirname);
    rg_storage_scandir(folder, scan_folder_cb, emu, RG_SCANDIR_RECURSIVE);
}

void emulator_show_file_info(retro_emulator_file_t *file)
{
    char filename_value[128];
    char type_value[32];
    char size_value[32];
#if COVERFLOW != 0
    char img_size[32];
#endif

    odroid_dialog_choice_t choices[] = {
        {-1, curr_lang->s_File, filename_value, 0, NULL},
        {-1, curr_lang->s_Type, type_value, 0, NULL},
        {-1, curr_lang->s_Size, size_value, 0, NULL},
		#if COVERFLOW != 0
        {-1, curr_lang->s_ImgSize, img_size, 0, NULL},
		#endif
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {1, curr_lang->s_Close, "", 1, NULL},
        ODROID_DIALOG_CHOICE_LAST
    };

    sprintf(choices[0].value, "%.127s", file->name);
    sprintf(choices[1].value, "%s", file->ext);
    sprintf(choices[2].value, "%d KB", (int)(file->size / 1024));
    #if COVERFLOW != 0
    sprintf(choices[3].value, "%d KB", file->img_size / 1024);
	#endif

    odroid_overlay_dialog(curr_lang->s_GameProp, choices, -1, &gui_redraw_callback);
}

#if CHEAT_CODES == 1
static bool cheat_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    bool is_on = odroid_settings_ActiveGameGenieCodes_is_enabled(CHOSEN_FILE->id, option->id);
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) 
    {
        is_on = is_on ? false : true;
        odroid_settings_ActiveGameGenieCodes_set(CHOSEN_FILE->id, option->id, is_on);
    }
    strcpy(option->value, is_on ? curr_lang->s_Cheat_Codes_ON : curr_lang->s_Cheat_Codes_OFF);
    return event == ODROID_DIALOG_ENTER;
}

static bool show_cheat_dialog()
{
    static odroid_dialog_choice_t last = ODROID_DIALOG_CHOICE_LAST;

    // +1 for the terminator sentinel
    odroid_dialog_choice_t *choices = rg_alloc((CHOSEN_FILE->cheat_count + 1) * sizeof(odroid_dialog_choice_t), MEM_ANY);
    char svalues[MAX_CHEAT_CODES][10];
    for(int i=0; i<CHOSEN_FILE->cheat_count; i++) 
    {
        const char *label = CHOSEN_FILE->cheat_descs[i];
        if (label == NULL) {
            label = CHOSEN_FILE->cheat_codes[i];
        }
        choices[i].id = i;
        choices[i].label = label;
        choices[i].value = svalues[i];
        choices[i].enabled = 1;
        choices[i].update_cb = cheat_update_cb;
    }
    choices[CHOSEN_FILE->cheat_count] = last;
    odroid_overlay_dialog(curr_lang->s_Cheat_Codes_Title, choices, 0, NULL);

    rg_free(choices);
    odroid_settings_commit();
    return false;
}
#endif

bool emulator_show_file_menu(retro_emulator_file_t *file)
{
    int slot = -1;
    CHOSEN_FILE = file;
    char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, file->path);
    rg_emu_states_t *savestates = odroid_system_emu_get_states(file->path, 4);
    bool has_save = savestates->used > 0;
    bool has_sram = odroid_sdcard_get_filesize(sram_path) > 0;
//    bool is_fav = favorite_find(file) != NULL;
    bool force_redraw = false;

#if CHEAT_CODES == 1
    odroid_dialog_choice_t last = ODROID_DIALOG_CHOICE_LAST;
    odroid_dialog_choice_t cheat_row = {4, curr_lang->s_Cheat_Codes, "", 1, NULL};
    odroid_dialog_choice_t cheat_choice = last; 
    if (CHOSEN_FILE->cheat_count != 0) {
        cheat_choice = cheat_row;
    }
#endif

    odroid_dialog_choice_t choices[] = {
        {0, curr_lang->s_Resume_game, "", has_save ? 1:-1, NULL},
        {1, curr_lang->s_New_game, "", 1, NULL},
        ODROID_DIALOG_CHOICE_SEPARATOR,
//        {3, is_fav ? "Del favorite" : "Add favorite", "", 1, NULL},
        {2, curr_lang->s_Delete_save, "", (has_save || has_sram) ? 1 : -1, NULL},
#if CHEAT_CODES == 1
        ODROID_DIALOG_CHOICE_SEPARATOR,
        cheat_choice,
#endif
        ODROID_DIALOG_CHOICE_LAST
    };

#if CHEAT_CODES == 1
    if (CHOSEN_FILE->cheat_count == 0)
        choices[4] = last;
#endif

    int sel = odroid_overlay_dialog(file->name, choices, has_save ? 0 : 1, &gui_redraw_callback);

    if (sel == 0) { // Resume game
        if ((slot = odroid_savestate_menu(curr_lang->s_Resume_game, file->path, true, &gui_redraw_callback)) != -1) {
            gui_save_current_tab();
            emulator_start(file, true, false, slot);
        }
    }
    if (sel == 1) { // New game
        gui_save_current_tab();
        emulator_start(file, false, false, 0);
    }
    else if (sel == 2) {
        while ((savestates->used > 0) &&
               ((slot = odroid_savestate_menu(curr_lang->s_Confirm_del_save, file->path, true, &gui_redraw_callback)) != -1))
        {
            odroid_sdcard_unlink(savestates->slots[slot].preview);
            odroid_sdcard_unlink(savestates->slots[slot].file);
            savestates->slots[slot].is_used = false;
            savestates->used--;
        }
        if (has_sram && odroid_overlay_confirm(curr_lang->s_Confirm_del_sram, false, &gui_redraw_callback))
        {
            odroid_sdcard_unlink(sram_path);
        }
    }
/*    else if (sel == 3) {
        if (is_fav)
            favorite_remove(file);
        else
            favorite_add(file);
    }*/
#if CHEAT_CODES == 1
    else if (sel == 4) {
        if (CHOSEN_FILE->cheat_count != 0)
            show_cheat_dialog();
        force_redraw = true;
    }
#endif

    free(sram_path);
    free(savestates);

    CHOSEN_FILE = NULL;

    return force_redraw;
}

void emulator_start(retro_emulator_file_t *file, bool load_state, bool start_paused, int8_t save_slot)
{
    printf("Retro-Go: Starting game: %s\n", file->name);
    // odroid_settings_StartAction_set(load_state ? ODROID_START_ACTION_RESUME : ODROID_START_ACTION_NEWGAME);
    // odroid_settings_commit();

    // create a copy in internal ram as ram used by ram_malloc will be erase by emulator
    retro_emulator_file_t *newfile = calloc(sizeof(retro_emulator_file_t),1);
    memcpy(newfile,file,sizeof(retro_emulator_file_t));
    newfile->name=calloc(strlen(file->name)+1,1);
    strcpy((char *)newfile->name,file->name);
    newfile->path=calloc(strlen(file->path)+1,1);
    strcpy(newfile->path,file->path);

    const char *system_name = newfile->system->system_name;

    // Copy game data from SD card to flash if needed
    if (newfile->system->game_data_type != NO_GAME_DATA) {
        uint32_t rounded_size = 1;
        while (rounded_size < newfile->size) {
            rounded_size <<= 1;
        }
        newfile->address = copy_file_to_cache(newfile->path,rounded_size,
                                           newfile->system->game_data_type == GAME_DATA_BYTESWAP_16);
        ACTIVE_FILE = newfile;
        ROM_DATA = newfile->address;
        ROM_EXT = "";//newfile->ext; // TODO : get correct const char * for this
        ROM_DATA_LENGTH = newfile->size;
    }

    if(strcmp(system_name, "Nintendo Gameboy") == 0) {
#if FORCE_GNUBOY == 1
        memcpy(&__RAM_EMU_START__, &_OVERLAY_GB_LOAD_START, (size_t)&_OVERLAY_GB_SIZE);
        memset(&_OVERLAY_GB_BSS_START, 0x0, (size_t)&_OVERLAY_GB_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_GB_SIZE);
        app_main_gb(load_state, start_paused, save_slot);
#else
//        copy_file_to_ram("/cores/gb/tgb-dual.bin",(char *)&__RAM_EMU_START__);
        memcpy(&__RAM_EMU_START__, &_OVERLAY_TGB_LOAD_START, (size_t)&_OVERLAY_TGB_SIZE);
        memset(&_OVERLAY_TGB_BSS_START, 0x0, (size_t)&_OVERLAY_TGB_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_TGB_SIZE);

        // Initializes the heap used by new and new[]
        cpp_heap_init((size_t) &_OVERLAY_TGB_BSS_END);

        app_main_gb_tgbdual(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Nintendo Entertainment System") == 0) {
#ifdef ENABLE_EMULATOR_NES
#if FORCE_NOFRENDO == 1
        memcpy(&__RAM_EMU_START__, &_OVERLAY_NES_LOAD_START, (size_t)&_OVERLAY_NES_SIZE);
        memset(&_OVERLAY_NES_BSS_START, 0x0, (size_t)&_OVERLAY_NES_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_NES_SIZE);
        app_main_nes(load_state, start_paused, save_slot);
#else
        memcpy(&__RAM_EMU_START__, &_OVERLAY_NES_FCEU_LOAD_START, (size_t)&_OVERLAY_NES_FCEU_SIZE);
        memset(&_OVERLAY_NES_FCEU_BSS_START, 0x0, (size_t)&_OVERLAY_NES_FCEU_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_NES_FCEU_SIZE);
        app_main_nes_fceu(load_state, start_paused, save_slot);
#endif
#endif
    } else if(strcmp(system_name, "Sega Master System") == 0 ||
              strcmp(system_name, "Sega Game Gear") == 0     ||
              strcmp(system_name, "Sega SG-1000") == 0       ||
              strcmp(system_name, "Colecovision") == 0 ) {
#if defined(ENABLE_EMULATOR_SMS) || defined(ENABLE_EMULATOR_GG) || defined(ENABLE_EMULATOR_COL) || defined(ENABLE_EMULATOR_SG1000)
        memcpy(&__RAM_EMU_START__, &_OVERLAY_SMS_LOAD_START, (size_t)&_OVERLAY_SMS_SIZE);
        memset(&_OVERLAY_SMS_BSS_START, 0x0, (size_t)&_OVERLAY_SMS_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_SMS_SIZE);
        if (! strcmp(system_name, "Colecovision")) app_main_smsplusgx(load_state, start_paused, save_slot, SMSPLUSGX_ENGINE_COLECO);
        else
        if (! strcmp(system_name, "Sega SG-1000")) app_main_smsplusgx(load_state, start_paused, save_slot, SMSPLUSGX_ENGINE_SG1000);
        else                                            app_main_smsplusgx(load_state, start_paused, save_slot, SMSPLUSGX_ENGINE_OTHERS);
#endif
    } else if(strcmp(system_name, "Game & Watch") == 0 ) {
#ifdef ENABLE_EMULATOR_GW
        memcpy(&__RAM_EMU_START__, &_OVERLAY_GW_LOAD_START, (size_t)&_OVERLAY_GW_SIZE);
        memset(&_OVERLAY_GW_BSS_START, 0x0, (size_t)&_OVERLAY_GW_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_GW_SIZE);
        app_main_gw(load_state, save_slot);
#endif
    } else if(strcmp(system_name, "PC Engine") == 0) {
#ifdef ENABLE_EMULATOR_PCE
      memcpy(&__RAM_EMU_START__, &_OVERLAY_PCE_LOAD_START, (size_t)&_OVERLAY_PCE_SIZE);
      memset(&_OVERLAY_PCE_BSS_START, 0x0, (size_t)&_OVERLAY_PCE_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_PCE_SIZE);
      app_main_pce(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "MSX") == 0) {
#ifdef ENABLE_EMULATOR_MSX
      memcpy(&__RAM_EMU_START__, &_OVERLAY_MSX_LOAD_START, (size_t)&_OVERLAY_MSX_SIZE);
      memset(&_OVERLAY_MSX_BSS_START, 0x0, (size_t)&_OVERLAY_MSX_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_MSX_SIZE);
      app_main_msx(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Watara Supervision") == 0) {
#ifdef ENABLE_EMULATOR_WSV
      memcpy(&__RAM_EMU_START__, &_OVERLAY_WSV_LOAD_START, (size_t)&_OVERLAY_WSV_SIZE);
      memset(&_OVERLAY_WSV_BSS_START, 0x0, (size_t)&_OVERLAY_WSV_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_WSV_SIZE);
      app_main_wsv(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Sega Genesis") == 0)  {
#ifdef ENABLE_EMULATOR_MD
      memcpy(&__RAM_EMU_START__, &_OVERLAY_MD_LOAD_START, (size_t)&_OVERLAY_MD_SIZE);
      memset(&_OVERLAY_MD_BSS_START, 0x0, (size_t)&_OVERLAY_MD_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_MD_SIZE);
      app_main_gwenesis(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Atari 2600") == 0) {
#ifdef ENABLE_EMULATOR_A2600
        memcpy(&__RAM_EMU_START__, &_OVERLAY_A2600_LOAD_START, (size_t)&_OVERLAY_A2600_SIZE);
        memset(&_OVERLAY_A2600_BSS_START, 0x0, (size_t)&_OVERLAY_A2600_BSS_SIZE);
        SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_A2600_SIZE);

        // Initializes the heap used by new and new[]
        cpp_heap_init((size_t) &_OVERLAY_A2600_BSS_END);

        app_main_a2600(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Atari 7800") == 0)  {
 #ifdef ENABLE_EMULATOR_A7800
      memcpy(&__RAM_EMU_START__, &_OVERLAY_A7800_LOAD_START, (size_t)&_OVERLAY_A7800_SIZE);
      memset(&_OVERLAY_A7800_BSS_START, 0x0, (size_t)&_OVERLAY_A7800_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_A7800_SIZE);
      app_main_a7800(load_state, start_paused, save_slot);
 #endif
    } else if(strcmp(system_name, "Amstrad CPC") == 0)  {
 #ifdef ENABLE_EMULATOR_AMSTRAD
      memcpy(&__RAM_EMU_START__, &_OVERLAY_AMSTRAD_LOAD_START, (size_t)&_OVERLAY_AMSTRAD_SIZE);
      memset(&_OVERLAY_AMSTRAD_BSS_START, 0x0, (size_t)&_OVERLAY_AMSTRAD_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_AMSTRAD_SIZE);
      app_main_amstrad(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Zelda3") == 0)  {
#ifdef ENABLE_HOMEBREW_ZELDA3
      memcpy(&__RAM_EMU_START__, &_OVERLAY_ZELDA3_LOAD_START, (size_t)&_OVERLAY_ZELDA3_SIZE);
      memset(&_OVERLAY_ZELDA3_BSS_START, 0x0, (size_t)&_OVERLAY_ZELDA3_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_ZELDA3_SIZE);
      app_main_zelda3(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "SMW") == 0)  {
#ifdef ENABLE_HOMEBREW_SMW
      memcpy(&__RAM_EMU_START__, &_OVERLAY_SMW_LOAD_START, (size_t)&_OVERLAY_SMW_SIZE);
      memset(&_OVERLAY_SMW_BSS_START, 0x0, (size_t)&_OVERLAY_SMW_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_SMW_SIZE);
      app_main_smw(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Philips Vectrex") == 0)  {
#ifdef ENABLE_EMULATOR_VIDEOPAC
      memcpy(&__RAM_EMU_START__, &_OVERLAY_VIDEOPAC_LOAD_START, (size_t)&_OVERLAY_VIDEOPAC_SIZE);
      memset(&_OVERLAY_VIDEOPAC_BSS_START, 0x0, (size_t)&_OVERLAY_VIDEOPAC_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_VIDEOPAC_SIZE);
      app_main_videopac(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Homebrew") == 0)  {
#ifdef ENABLE_EMULATOR_CELESTE
      memcpy(&__RAM_EMU_START__, &_OVERLAY_CELESTE_LOAD_START, (size_t)&_OVERLAY_CELESTE_SIZE);
      memset(&_OVERLAY_CELESTE_BSS_START, 0x0, (size_t)&_OVERLAY_CELESTE_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_CELESTE_SIZE);
      app_main_celeste(load_state, start_paused, save_slot);
#endif
    } else if(strcmp(system_name, "Tamagotchi") == 0) {
#ifdef ENABLE_EMULATOR_TAMA
      memcpy(&__RAM_EMU_START__, &_OVERLAY_TAMA_LOAD_START, (size_t)&_OVERLAY_TAMA_SIZE);
      memset(&_OVERLAY_TAMA_BSS_START, 0x0, (size_t)&_OVERLAY_TAMA_BSS_SIZE);
      SCB_CleanDCache_by_Addr((uint32_t *)&__RAM_EMU_START__, (size_t)&_OVERLAY_TAMA_SIZE);
      app_main_tama(load_state, start_paused, save_slot);
#endif
    }
}

void emulators_init()
{
    add_emulator("Nintendo Gameboy", "gb", "gb gbc", &pad_gb, &header_gb, GAME_DATA);
    add_emulator("Nintendo Entertainment System", "nes", "nes", &pad_nes, &header_nes, GAME_DATA);
//    add_emulator("Game & Watch", "gw", "gw", &pad_gw, &header_gw, GAME_DATA);
//    add_emulator("PC Engine", "pce", "pce", &pad_pce, &header_pce, GAME_DATA);
//    add_emulator("Sega Game Gear", "gg", "gg", &pad_gg, &header_gg, GAME_DATA);
//    add_emulator("Sega Master System", "sms", "sms", &pad_sms, &header_sms, GAME_DATA);
    add_emulator("Sega Genesis", "md", "md gen bin", &pad_gen, &header_gen, GAME_DATA_BYTESWAP_16);
//    add_emulator("Homebrew", "homebrew", "bin", &pad_homebrew, &header_homebrew, NO_GAME_DATA);
//    add_emulator("Sega Genesis", "md", "md gen bin", -1, 0, &pad_gen, &header_gen, GAME_DATA_BYTESWAP_16);
/*
    add_emulator("Nintendo Gameboy", "gb", "gb gbc", "tgbdual-go", 0, &pad_gb, &header_gb);
    add_emulator("Nintendo Entertainment System", "nes", "nes fc fds nsf", "fceumm", 16, &pad_nes, &header_nes);
    add_emulator("Game & Watch", "gw", "gw", "LCD-Game-Emulator", 0, &pad_gw, &header_gw);
    add_emulator("PC Engine", "pce", "pce", "pce-go", 0, &pad_pce, &header_pce);
    add_emulator("Sega Game Gear", "gg", "gg", "smsplusgx-go", 0, &pad_gg, &header_gg);
    add_emulator("Sega Master System", "sms", "sms", "smsplusgx-go", 0, &pad_sms, &header_sms);
    add_emulator("Sega Genesis", "md", "md gen bin", "gwenesis", 0, &pad_gen, &header_gen);
    add_emulator("Sega SG-1000", "sg", "sg", "smsplusgx-go", 0, &pad_sg1000, &header_sg1000);
    add_emulator("Colecovision", "col", "col", "smsplusgx-go", 0, &pad_col, &header_col);
    add_emulator("Watara Supervision", "wsv", "wsv", "potator", 0, &pad_wsv, &header_wsv);
//    add_emulator("Atari 2600", "a2600", "a2600", "stella2014-go", 0, &pad_a7800, &header_a7800); // TODO : add specific gfx
    add_emulator("Atari 7800", "a7800", "a7800", "prosystem-go", 0, &pad_a7800, &header_a7800);
    add_emulator("Amstrad CPC", "amstrad", "amstrad", "caprice32", 0, &pad_amstrad, &header_amstrad);
//    add_emulator("Philips Vectrex", "videopac", "bin", "o2em-go", 0, &pad_gb, &header_gb); // TODO Sylver : change graphics
    add_emulator("Tamagotchi", "tama", "b", "tamalib", 0, &pad_tama, &header_tama);*/
//    while(1);
#if 0
#if !( defined(ENABLE_EMULATOR_GB) || defined(ENABLE_EMULATOR_NES) || defined(ENABLE_EMULATOR_SMS) || defined(ENABLE_EMULATOR_GG) || defined(ENABLE_EMULATOR_COL) || defined(ENABLE_EMULATOR_SG1000) || defined(ENABLE_EMULATOR_PCE) || defined(ENABLE_EMULATOR_GW) || defined(ENABLE_EMULATOR_MSX) || defined(ENABLE_EMULATOR_WSV) || defined(ENABLE_EMULATOR_MD) || defined(ENABLE_EMULATOR_A7800) || defined(ENABLE_EMULATOR_AMSTRAD) || defined(ENABLE_EMULATOR_VIDEOPAC) || defined(ENABLE_HOMEBREW)|| defined(ENABLE_HOMEBREW_ZELDA3) || defined(ENABLE_HOMEBREW_SMW) || defined(ENABLE_EMULATOR_TAMA) || defined(ENABLE_EMULATOR_A2600))
    // Add gameboy as a placeholder in case no emulator is built.
    add_emulator("Nintendo Gameboy", "gb", "gb", "tgbdual-go", 0, &pad_gb, &header_gb);
#endif


#ifdef ENABLE_EMULATOR_GB
    add_emulator("Nintendo Gameboy", "gb", "gb", "tgbdual-go", 0, &pad_gb, &header_gb);
    // add_emulator("Nintendo Gameboy Color", "gbc", "gbc", "gnuboy-go", 0, logo_gbc, header_gbc);
#endif

#ifdef ENABLE_EMULATOR_NES
    add_emulator("Nintendo Entertainment System", "nes", "nes", "nofrendo-go", 16, &pad_nes, &header_nes);
#endif
    
#ifdef ENABLE_EMULATOR_GW
    add_emulator("Game & Watch", "gw", "gw", "LCD-Game-Emulator", 0, &pad_gw, &header_gw);
#endif

#ifdef ENABLE_EMULATOR_PCE
    add_emulator("PC Engine", "pce", "pce", "pce-go", 0, &pad_pce, &header_pce);
#endif

#ifdef ENABLE_EMULATOR_GG
    add_emulator("Sega Game Gear", "gg", "gg", "smsplusgx-go", 0, &pad_gg, &header_gg);
#endif

#ifdef ENABLE_EMULATOR_SMS
    add_emulator("Sega Master System", "sms", "sms", "smsplusgx-go", 0, &pad_sms, &header_sms);
#endif

#ifdef ENABLE_EMULATOR_MD
    add_emulator("Sega Genesis", "md", "md", "GnWesis", 0, &pad_gen, &header_gen);
#endif

#ifdef ENABLE_EMULATOR_SG1000
    add_emulator("Sega SG-1000", "sg", "sg", "smsplusgx-go", 0, &pad_sg1000, &header_sg1000);
#endif

#ifdef ENABLE_EMULATOR_COL
    add_emulator("Colecovision", "col", "col", "smsplusgx-go", 0, &pad_col, &header_col);
#endif

#ifdef ENABLE_EMULATOR_MSX
    add_emulator("MSX", "msx", "msx", "blueMSX", 0, &pad_msx, &header_msx);
#endif

#ifdef ENABLE_EMULATOR_WSV
    add_emulator("Watara Supervision", "wsv", "wsv", "potator", 0, &pad_wsv, &header_wsv);
#endif

#ifdef ENABLE_EMULATOR_A2600
    add_emulator("Atari 2600", "a2600", "a2600", "stella2014-go", 0, &pad_a7800, &header_a7800); // TODO : add specific gfx^M
#endif

#ifdef ENABLE_EMULATOR_A7800
    add_emulator("Atari 7800", "a7800", "a7800", "prosystem-go", 0, &pad_a7800, &header_a7800);
#endif

#ifdef ENABLE_EMULATOR_AMSTRAD
    add_emulator("Amstrad CPC", "amstrad", "amstrad", "caprice32", 0, &pad_amstrad, &header_amstrad);
#endif

#ifdef ENABLE_HOMEBREW_ZELDA3
    add_emulator("Zelda3", "zelda3", "zelda3", "zelda3", 0, &pad_snes, &header_zelda3);
#endif

#ifdef ENABLE_HOMEBREW_SMW
    add_emulator("SMW", "smw", "smw", "smw", 0, &pad_snes, &header_smw);
#endif

#ifdef ENABLE_EMULATOR_VIDEOPAC
    add_emulator("Philips Vectrex", "videopac", "bin", "o2em-go", 0, &pad_gb, &header_gb); // TODO Sylver : change graphics
#endif

#ifdef ENABLE_HOMEBREW
    add_emulator("Homebrew", "homebrew", "bin", "", 0, &pad_homebrew, &header_homebrew);
#endif

#ifdef ENABLE_EMULATOR_TAMA
    add_emulator("Tamagotchi", "tama", "b", "tamalib", 0, &pad_tama, &header_tama);
#endif

    // add_emulator("ColecoVision", "col", "col", "smsplusgx-go", 0, logo_col, header_col);
    // add_emulator("PC Engine", "pce", "pce", "huexpress-go", 0, logo_pce, header_pce);
    // add_emulator("Atari Lynx", "lnx", "lnx", "handy-go", 64, logo_lnx, header_lnx);
    // add_emulator("Atari 2600", "a26", "a26", "stella-go", 0, logo_a26, header_a26);
#endif
}

bool emulator_is_file_valid(retro_emulator_file_t *file)
{
    for (int i = 0; i < emulators_count; i++) {
        for (int j = 0; j < emulators[i].roms.count; j++) {
            if (&emulators[i].roms.files[j] == file) {
                return true;
            }
        }
    }

    return false;
}
