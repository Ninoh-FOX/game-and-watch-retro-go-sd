#include <odroid_system.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>


#include "main.h"
#include "appid.h"

/* TO move elsewhere */
#include "stm32h7xx_hal.h"

#include "common.h"
#include "rom_manager.h"
#include "gw_lcd.h"

#include "MSX.h"
#include "SoundMSX.h"
#include "I8251.h"

#include "main_msx.h"
#include "video_msx.h"
#include "core_msx.h"

#define MSX_AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60)
#define AUDIO_BUFFER_LENGTH_DMA_MSX (2 * MSX_AUDIO_BUFFER_LENGTH)
static sample msxAudioBuffer[2*MSX_AUDIO_BUFFER_LENGTH];
static int16_t msxAudioBufferOffset;
static bool msx_audio_init = false;


static unsigned char msx_joystick_state = 0;
static odroid_gamepad_state_t previous_joystick_state;
int msx_button_a_key_index = 5; /* KBD_SPACE index */
int msx_button_b_key_index = 43; /* n key index */

static bool msx_system_LoadState(char *pathName)
{
    int size = LoadMsxStateFlash(ACTIVE_FILE->save_address);
    printf("LoadState %d",size);
    return true;
}

static bool msx_system_SaveState(char *pathName)
{
    int size = SaveMsxStateFlash(ACTIVE_FILE->save_address, ACTIVE_FILE->save_size);
    printf("SaveState %d",size);
    return true;
}

/** PlayAllSound() *******************************************/
/** Render and play given number of microseconds of sound.  **/
/*************************************************************/
void PlayAllSound(int uSec)
{
      /* +1 to avoid skipping */
      RenderAndPlayAudio(uSec*AUDIO_SAMPLE_RATE/1000000+1);
}

/* Returns the number of samples we can fill in the audio buffer */
unsigned int GetFreeAudio(void) {
  return MSX_AUDIO_BUFFER_LENGTH-msxAudioBufferOffset/2;
}

unsigned int WriteAudio(sample *Data,unsigned int Length) {
    uint8_t volume = odroid_audio_volume_get();
    int32_t factor = volume_tbl[volume];

    for (int i = 0; i < Length; i++) {
        int32_t sample = Data[i];
        if (audio_mute || volume == ODROID_AUDIO_VOLUME_MIN) {
                msxAudioBuffer[msxAudioBufferOffset] = 0;
        } else {
                msxAudioBuffer[msxAudioBufferOffset] = (sample * factor) >> 8;
        }
        msxAudioBufferOffset++;
    }
    return Length;
}

/** TrashAudio() *********************************************/
/** Free resources allocated by InitAudio().                **/
/*************************************************************/
void TrashAudio(void)
{
}

unsigned int InitAudio(unsigned int Rate,unsigned int Latency) {
      // Init Sound
      msxAudioBufferOffset = 0;
      memset(msxAudioBuffer, 0, MSX_AUDIO_BUFFER_LENGTH*sizeof(sample));
      memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
      HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audiobuffer_dma, AUDIO_BUFFER_LENGTH_DMA_MSX);

      printf("Sound initialized\n");
      return Rate;
}

static int selected_disk_index = 0;
#define MSX_DISK_EXTENSION "fdi"
static bool update_disk_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int disk_count = 0;
    int max_index = 0;
    retro_emulator_file_t *disk_file = NULL;
    rom_system_t *msx_system = rom_manager_system(&rom_mgr, "MSX");
    disk_count = rom_get_ext_count(msx_system,MSX_DISK_EXTENSION);
    if (disk_count > 0) {
        max_index = disk_count - 1;
    } else {
        max_index = 0;
    }

    if (event == ODROID_DIALOG_PREV) {
        selected_disk_index = selected_disk_index > 0 ? selected_disk_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        selected_disk_index = selected_disk_index < max_index ? selected_disk_index + 1 : 0;
    }

    disk_file = rom_get_ext_file_at_index(msx_system,MSX_DISK_EXTENSION,selected_disk_index);
    if (event == ODROID_DIALOG_ENTER) {
        if (disk_count > 0) {
            msx_change_disk(0,disk_file->name,disk_file->address);
        }
    }
    if (disk_count > 0) {
    strcpy(option->value, disk_file->name);
    } else {
    strcpy(option->value, "No disk");
    }
    return event == ODROID_DIALOG_ENTER;
}

// Default is MSX2+
int selected_msx_index = 2;

static bool update_msx_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
  int max_index = 2;
  int mode = Mode;

  if (event == ODROID_DIALOG_PREV) {
        selected_msx_index = selected_msx_index > 0 ? selected_msx_index - 1 : max_index;
  }
  if (event == ODROID_DIALOG_NEXT) {
        selected_msx_index = selected_msx_index < max_index ? selected_msx_index + 1 : 0;
  }

  switch (selected_msx_index) {
    case 0: // MSX1;
            strcpy(option->value, "MSX1");
            break;
    case 1: // MSX2;
            strcpy(option->value, "MSX2");
            break;
    case 2: // MSX2+;
            strcpy(option->value, "MSX2+");
            break;
  }

  if (event == ODROID_DIALOG_ENTER) {
        switch (selected_msx_index) {
              case 0: // MSX1;
                    mode = (mode & ~(MSX_MODEL)) | MSX_MSX1;
                    break;
              case 1: // MSX2;
                    mode = (mode & ~(MSX_MODEL)) | MSX_MSX2;
                    break;
              case 2: // MSX2+;
                    mode = (mode & ~(MSX_MODEL)) | MSX_MSX2P;
                    break;
        }
        msx_start(mode,RAMPages,VRAMPages,NULL);
  }
   return event == ODROID_DIALOG_ENTER;
}

struct msx_key_info {
    int  key_id;
    const char *name;
};

struct msx_key_info msx_keyboard[] = {
    {KBD_F1,"F1"},
    {KBD_F2,"F2"},
    {KBD_F3,"F3"},
    {KBD_F4,"F4"},
    {KBD_F5,"F5"},
    {KBD_SPACE,"Space"},
    {KBD_SHIFT,"Shift"},
    {KBD_CONTROL,"Control"},
    {KBD_GRAPH,"Graph"},
    {KBD_BS,"BS"},
    {KBD_TAB,"Tab"},
    {KBD_CAPSLOCK,"CapsLock"},
    {KBD_SELECT,"Select"},
    {KBD_HOME,"Home"},
    {KBD_ENTER,"Enter"},
    {KBD_DELETE,"Delete"},
    {KBD_INSERT,"Insert"},
    {KBD_COUNTRY,"Country"},
    {KBD_STOP,"Stop"},
    {KBD_ESCAPE,"Esc"},
    {KBD_NUMPAD0,"0"},
    {KBD_NUMPAD1,"1"},
    {KBD_NUMPAD2,"2"},
    {KBD_NUMPAD3,"3"},
    {KBD_NUMPAD4,"4"},
    {KBD_NUMPAD5,"5"},
    {KBD_NUMPAD6,"6"},
    {KBD_NUMPAD7,"7"},
    {KBD_NUMPAD8,"8"},
    {KBD_NUMPAD9,"9"},
    {97,"a"},
    {98,"b"},
    {99,"c"},
    {100,"d"},
    {101,"e"},
    {102,"f"},
    {103,"g"},
    {104,"h"},
    {105,"i"},
    {106,"j"},
    {107,"k"},
    {108,"l"},
    {109,"m"},
    {110,"n"},
    {111,"o"},
    {112,"p"},
    {113,"q"},
    {114,"r"},
    {115,"s"},
    {116,"t"},
    {117,"u"},
    {118,"v"},
    {119,"w"},
    {120,"x"},
    {121,"y"},
    {122,"z"},
};

#define RELEASE_KEY_DELAY 5
static int selected_key_index = 0;
static int pressed_key = 0;
static int release_key = 0;
static int release_key_delay = RELEASE_KEY_DELAY;
static bool update_keyboard_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(msx_keyboard)/sizeof(msx_keyboard[0])-1;

    if (event == ODROID_DIALOG_PREV) {
        selected_key_index = selected_key_index > 0 ? selected_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        selected_key_index = selected_key_index < max_index ? selected_key_index + 1 : 0;
    }

    strcpy(option->value, msx_keyboard[selected_key_index].name);

    if (event == ODROID_DIALOG_ENTER) {
        pressed_key = msx_keyboard[selected_key_index].key_id;
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool update_a_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(msx_keyboard)/sizeof(msx_keyboard[0])-1;

    if (event == ODROID_DIALOG_PREV) {
        msx_button_a_key_index = msx_button_a_key_index > 0 ? msx_button_a_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        msx_button_a_key_index = msx_button_a_key_index < max_index ? msx_button_a_key_index + 1 : 0;
    }

    strcpy(option->value, msx_keyboard[msx_button_a_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_b_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(msx_keyboard)/sizeof(msx_keyboard[0])-1;

    if (event == ODROID_DIALOG_PREV) {
        msx_button_b_key_index = msx_button_b_key_index > 0 ? msx_button_b_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        msx_button_b_key_index = msx_button_b_key_index < max_index ? msx_button_b_key_index + 1 : 0;
    }

    strcpy(option->value, msx_keyboard[msx_button_b_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

/** Joystick() ***********************************************/
/** Query positions of two joystick connected to ports 0/1. **/
/** Returns 0.0.B2.A2.R2.L2.D2.U2.0.0.B1.A1.R1.L1.D1.U1.    **/
/*************************************************************/
unsigned int Joystick(void)
{
    odroid_gamepad_state_t joystick;

    odroid_input_read_gamepad(&joystick);

    if ((joystick.values[ODROID_INPUT_LEFT]) && !previous_joystick_state.values[ODROID_INPUT_LEFT]) {
        KBD_SET(KBD_LEFT);
    } else if (!(joystick.values[ODROID_INPUT_LEFT]) && previous_joystick_state.values[ODROID_INPUT_LEFT]) {
        KBD_RES(KBD_LEFT);
    }
    if ((joystick.values[ODROID_INPUT_RIGHT]) && !previous_joystick_state.values[ODROID_INPUT_RIGHT]) {
        KBD_SET(KBD_RIGHT);
    } else if (!(joystick.values[ODROID_INPUT_RIGHT]) && previous_joystick_state.values[ODROID_INPUT_RIGHT]) {
        KBD_RES(KBD_RIGHT);
    }
    if ((joystick.values[ODROID_INPUT_UP]) && !previous_joystick_state.values[ODROID_INPUT_UP]) {
        KBD_SET(KBD_UP);
    } else if (!(joystick.values[ODROID_INPUT_UP]) && previous_joystick_state.values[ODROID_INPUT_UP]) {
        KBD_RES(KBD_UP);
    }
    if ((joystick.values[ODROID_INPUT_DOWN]) && !previous_joystick_state.values[ODROID_INPUT_DOWN]) {
        KBD_SET(KBD_DOWN);
    } else if (!(joystick.values[ODROID_INPUT_DOWN]) && previous_joystick_state.values[ODROID_INPUT_DOWN]) {
        KBD_RES(KBD_DOWN);
    }
    if ((joystick.values[ODROID_INPUT_A]) && !previous_joystick_state.values[ODROID_INPUT_A]) {
        KBD_SET(msx_keyboard[msx_button_a_key_index].key_id);
    } else if (!(joystick.values[ODROID_INPUT_A]) && previous_joystick_state.values[ODROID_INPUT_A]) {
        KBD_RES(msx_keyboard[msx_button_a_key_index].key_id);
    }
    if ((joystick.values[ODROID_INPUT_B]) && !previous_joystick_state.values[ODROID_INPUT_B]) {
        KBD_SET(msx_keyboard[msx_button_b_key_index].key_id);
    } else if (!(joystick.values[ODROID_INPUT_B]) && previous_joystick_state.values[ODROID_INPUT_B]) {
        KBD_RES(msx_keyboard[msx_button_b_key_index].key_id);
    }
    if ((joystick.values[ODROID_INPUT_START]) && !previous_joystick_state.values[ODROID_INPUT_START]) {
        KBD_SET(KBD_F5);
    } else if (!(joystick.values[ODROID_INPUT_START]) && previous_joystick_state.values[ODROID_INPUT_START]) {
        KBD_RES(KBD_F5);
    }
    if ((joystick.values[ODROID_INPUT_SELECT]) && !previous_joystick_state.values[ODROID_INPUT_SELECT]) {
        KBD_SET(KBD_ENTER);
    } else if (!(joystick.values[ODROID_INPUT_SELECT]) && previous_joystick_state.values[ODROID_INPUT_SELECT]) {
        KBD_RES(KBD_ENTER);
    }

    // Handle keyboard emulation
    if (pressed_key) {
        KBD_SET(pressed_key);
        release_key = pressed_key;
        pressed_key = 0;
    } else if (release_key) {
        if (release_key_delay == 0) {
            KBD_RES(release_key);
            release_key = 0;
            release_key_delay = RELEASE_KEY_DELAY;
        } else {
            release_key_delay--;
        }
    }

    memcpy(&previous_joystick_state,&joystick,sizeof(odroid_gamepad_state_t));

    return(msx_joystick_state);
}

/** Keyboard() ***********************************************/
/** Modify keyboard matrix.                                 **/
/*************************************************************/
void Keyboard(void)
{
  /* Everything is done in Joystick() */
}

static void createOptionMenu(odroid_dialog_choice_t *options) {
    char disk_name[128];
    char msx_name[6];
    char key_name[6];
    char a_button_name[6];
    char b_button_name[6];
    int index=0;
    if (strcmp(ROM_EXT,"fdi") == 0) {
        options[index].id = 100;
        options[index].label = "Change Dsk";
        options[index].value = disk_name;
        options[index].enabled = 1;
        options[index].update_cb = &update_disk_cb;
        index++;
    }
    options[index].id = 100;
    options[index].label = "Select MSX";
    options[index].value = msx_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_msx_cb;
    index++;
    options[index].id = 100;
    options[index].label = "A Button";
    options[index].value = a_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_a_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = "B Button";
    options[index].value = b_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_b_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = "Press Key";
    options[index].value = key_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_keyboard_cb;
    index++;
    options[index].id = 0x0F0F0F0F;
    options[index].label = "LAST";
    options[index].value = "LAST";
    options[index].enabled = 0xFFFF;
    options[index].update_cb = NULL;
}

/** RefreshScreen() ******************************************/
/** Refresh screen. This function is called in the end of   **/
/** refresh cycle to show the entire screen.                **/
/*************************************************************/
void RefreshScreen(void) {
    odroid_dialog_choice_t options[10];

    bool drawFrame = common_emu_frame_loop();

    wdog_refresh();

    odroid_gamepad_state_t joystick;
    odroid_input_read_gamepad(&joystick);

    createOptionMenu(options);
    common_emu_input_loop(&joystick, options);

    if (drawFrame) {
        if (!msx_audio_init) {
            // To make sure we have filled DMA with fresh sample before starting playing
            msx_audio_init=true;
            size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : MSX_AUDIO_BUFFER_LENGTH;
            memcpy(&audiobuffer_dma[offset],msxAudioBuffer,MSX_AUDIO_BUFFER_LENGTH*2);
            msxAudioBufferOffset = 0;
        } else {
            size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : MSX_AUDIO_BUFFER_LENGTH;
            memcpy(&audiobuffer_dma[offset],msxAudioBuffer,MSX_AUDIO_BUFFER_LENGTH*2);
            msxAudioBufferOffset = 0;
            HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audiobuffer_dma, AUDIO_BUFFER_LENGTH_DMA_MSX);
        }

        // remove used samples if needed
        common_ingame_overlay();
        lcd_swap();
    }
    if(!common_emu_state.skip_frames) {
        dma_transfer_state_t last_dma_state = DMA_TRANSFER_STATE_HF;
        for(uint8_t p = 0; p < common_emu_state.pause_frames + 1; p++) {
            while (dma_state == last_dma_state) {
                cpumon_sleep();
            }
            last_dma_state = dma_state;
        }
    }
}

/* Main */
void app_main_msx(uint8_t load_state, uint8_t start_paused)
{
    int SndSwitch;             /* Mask of enabled sound channels */
    int SndVolume;             /* Master volume for audio        */

    unsigned char *savestate_address = NULL;
    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / 60/*FPS_NTSC*/ + 0.5f);

    odroid_system_init(APPID_MSX, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&msx_system_LoadState, &msx_system_SaveState, NULL);

    /* Setup color palettes */
    msx_setup_palette();

    /* Init controls */
    memset(&previous_joystick_state,0, sizeof(odroid_gamepad_state_t));
    SETJOYTYPE(0,JOY_STICK);

    /* Init Sound */
    InitSound(AUDIO_SAMPLE_RATE,1000/60);
    SndSwitch=(1<<MAXCHANNELS)-1;
    SndVolume=64;
    SetChannels(SndVolume,SndSwitch);

    if (load_state) {
        savestate_address = (unsigned char *)ACTIVE_FILE->save_address;
    }
    msx_start(MSX_MSX2P|MSX_NTSC|MSX_MSXDOS2|MSX_GUESSA|MSX_GUESSB,
              RAMPages,VRAMPages,savestate_address);
}
