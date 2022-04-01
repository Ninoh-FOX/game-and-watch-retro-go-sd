#include <odroid_system.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>


#include "main.h"
#include "appid.h"

#include "stm32h7xx_hal.h"

#include "common.h"
#include "rom_manager.h"
#include "gw_lcd.h"

#include "MSX.h"
#include "Properties.h"
#include "ArchFile.h"
#include "VideoRender.h"
#include "AudioMixer.h"
#include "Casette.h"
#include "PrinterIO.h"
#include "UartIO.h"
#include "MidiIO.h"
#include "Machine.h"
#include "Board.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Actions.h"
#include "Language.h"
#include "LaunchFile.h"
#include "ArchEvent.h"
#include "ArchSound.h"
#include "ArchNotifications.h"
#include "JoystickPort.h"
#include "InputEvent.h"
#include "R800.h"

static Properties* properties;
extern BoardInfo boardInfo;
static Mixer* mixer;
static Video* video;

static odroid_gamepad_state_t previous_joystick_state;
int msx_button_a_key_index = 5; /* EC_SPACE index */
int msx_button_b_key_index = 51; /* n key index */

/* strings for options */
static char disk_name[128];
static char msx_name[6];
static char key_name[6];
static char a_button_name[6];
static char b_button_name[6];

static unsigned image_buffer_base_width;
static unsigned image_buffer_current_width;
static unsigned image_buffer_height;
static unsigned width = 272;
static unsigned height = 240;
static int double_width;

#define FPS_NTSC  60
#define FPS_PAL   50

#define FPS_MSX FPS_PAL

#define AUDIO_MSX_SAMPLE_RATE 22050
#define MSX_AUDIO_BUFFER_LENGTH (AUDIO_MSX_SAMPLE_RATE / FPS_MSX)
#define AUDIO_BUFFER_LENGTH_DMA_MSX (2 * MSX_AUDIO_BUFFER_LENGTH)
static int16_t msxAudioBufferOffset;

static bool msx_system_LoadState(char *pathName)
{
    return true;
}

static bool msx_system_SaveState(char *pathName)
{
    return true;
}

/* Core stubs */
void frameBufferDataDestroy(FrameBufferData* frameData){}
void frameBufferSetActive(FrameBufferData* frameData){}
void frameBufferSetMixMode(FrameBufferMixMode mode, FrameBufferMixMode mask){}
void frameBufferClearDeinterlace(){}
void frameBufferSetInterlace(FrameBuffer* frameBuffer, int val){}
void archTrap(UInt8 value){}
void videoUpdateAll(Video* video, Properties* properties){}

/* framebuffer */

uint16_t* frameBufferGetLine(FrameBuffer* frameBuffer, int y)
{
   return (lcd_get_active_buffer() + sizeof(uint16_t) * (y * image_buffer_current_width + 24));
}

FrameBuffer* frameBufferGetDrawFrame(void)
{
   return (void*)lcd_get_active_buffer();
}

FrameBuffer* frameBufferFlipDrawFrame(void)
{
   return (void*)lcd_get_active_buffer();
}

static int fbScanLine = 0;

void frameBufferSetScanline(int scanline)
{
   fbScanLine = scanline;
}

int frameBufferGetScanline(void)
{
   return fbScanLine;
}

FrameBufferData* frameBufferDataCreate(int maxWidth, int maxHeight, int defaultHorizZoom)
{
   return (void*)lcd_get_active_buffer();
}

FrameBufferData* frameBufferGetActive()
{
    return (void*)lcd_get_active_buffer();
}

void   frameBufferSetLineCount(FrameBuffer* frameBuffer, int val)
{
   image_buffer_height = val;
}

int    frameBufferGetLineCount(FrameBuffer* frameBuffer) {
   return image_buffer_height;
}

int frameBufferGetMaxWidth(FrameBuffer* frameBuffer)
{
   return FB_MAX_LINE_WIDTH;
}

int frameBufferGetDoubleWidth(FrameBuffer* frameBuffer, int y)
{
   return double_width;
}

void frameBufferSetDoubleWidth(FrameBuffer* frameBuffer, int y, int val)
{
}

static int selected_disk_index = 0;
#define MSX_DISK_EXTENSION "dsk"
static bool update_disk_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    char game_name[80];
    int disk_count = 0;
    int max_index = 0;
    retro_emulator_file_t *disk_file = NULL;
    const rom_system_t *msx_system = rom_manager_system(&rom_mgr, "MSX");
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

    disk_file = (retro_emulator_file_t *)rom_get_ext_file_at_index(msx_system,MSX_DISK_EXTENSION,selected_disk_index);
    if (event == ODROID_DIALOG_ENTER) {
        if (disk_count > 0) {
            sprintf(game_name,"%s.%s",disk_file->name,disk_file->ext);
            emulatorSuspend();
            insertDiskette(properties, 0, game_name, NULL, -1);
            emulatorResume();
        }
    }
    strcpy(option->value, disk_file->name);
    return event == ODROID_DIALOG_ENTER;
}

// Default is MSX2+
int selected_msx_index = 2;

static bool update_msx_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
  int max_index = 2;

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
//                    mode = (mode & ~(MSX_MODEL)) | MSX_MSX1;
                    break;
              case 1: // MSX2;
//                    mode = (mode & ~(MSX_MODEL)) | MSX_MSX2;
                    break;
              case 2: // MSX2+;
//                    mode = (mode & ~(MSX_MODEL)) | MSX_MSX2P;
                    break;
        }
//        msx_start(mode,RAMPages,VRAMPages,NULL);
  }
   return event == ODROID_DIALOG_ENTER;
}

struct msx_key_info {
    int  key_id;
    const char *name;
};

struct msx_key_info msx_keyboard[] = {
    {EC_F1,"F1"},
    {EC_F2,"F2"},
    {EC_F3,"F3"},
    {EC_F4,"F4"},
    {EC_F5,"F5"},
    {EC_SPACE,"Space"},
    {EC_LSHIFT,"Shift"},
    {EC_CTRL,"Control"},
    {EC_GRAPH,"Graph"},
    {EC_BKSPACE,"BS"},
    {EC_TAB,"Tab"},
    {EC_CAPS,"CapsLock"},
    {EC_SELECT,"Select"},
    {EC_RETURN,"Return"},
    {EC_DEL,"Delete"},
    {EC_INS,"Insert"},
    {EC_STOP,"Stop"},
    {EC_ESC,"Esc"},
    {EC_1,"1/!"},
    {EC_2,"2/@"},
    {EC_3,"3/#"},
    {EC_4,"4/$"},
    {EC_5,"5/%%"},
    {EC_6,"6/^"},
    {EC_7,"7/&"},
    {EC_8,"8/*"},
    {EC_9,"9/("},
    {EC_0,"0/)"},
    {EC_NUM0,"0"},
    {EC_NUM1,"1"},
    {EC_NUM2,"2"},
    {EC_NUM3,"3"},
    {EC_NUM4,"4"},
    {EC_NUM5,"5"},
    {EC_NUM6,"6"},
    {EC_NUM7,"7"},
    {EC_NUM8,"8"},
    {EC_NUM9,"9"},
    {EC_A,"a"},
    {EC_B,"b"},
    {EC_C,"c"},
    {EC_D,"d"},
    {EC_E,"e"},
    {EC_F,"f"},
    {EC_G,"g"},
    {EC_H,"h"},
    {EC_I,"i"},
    {EC_J,"j"},
    {EC_K,"k"},
    {EC_L,"l"},
    {EC_M,"m"},
    {EC_N,"n"},
    {EC_O,"o"},
    {EC_P,"p"},
    {EC_Q,"q"},
    {EC_R,"r"},
    {EC_S,"s"},
    {EC_T,"t"},
    {EC_U,"u"},
    {EC_V,"v"},
    {EC_W,"w"},
    {EC_X,"x"},
    {EC_Y,"y"},
    {EC_Z,"z"},
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

static void msxInputUpdate(odroid_gamepad_state_t *joystick)
{
    if ((joystick->values[ODROID_INPUT_LEFT]) && !previous_joystick_state.values[ODROID_INPUT_LEFT]) {
        eventMap[EC_LEFT]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_LEFT]) && previous_joystick_state.values[ODROID_INPUT_LEFT]) {
        eventMap[EC_LEFT]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_RIGHT]) && !previous_joystick_state.values[ODROID_INPUT_RIGHT]) {
        eventMap[EC_RIGHT]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_RIGHT]) && previous_joystick_state.values[ODROID_INPUT_RIGHT]) {
        eventMap[EC_RIGHT]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_UP]) && !previous_joystick_state.values[ODROID_INPUT_UP]) {
        eventMap[EC_UP]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_UP]) && previous_joystick_state.values[ODROID_INPUT_UP]) {
        eventMap[EC_UP]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_DOWN]) && !previous_joystick_state.values[ODROID_INPUT_DOWN]) {
        eventMap[EC_DOWN]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_DOWN]) && previous_joystick_state.values[ODROID_INPUT_DOWN]) {
        eventMap[EC_DOWN]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_A]) && !previous_joystick_state.values[ODROID_INPUT_A]) {
        eventMap[msx_keyboard[msx_button_a_key_index].key_id]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_A]) && previous_joystick_state.values[ODROID_INPUT_A]) {
        eventMap[msx_keyboard[msx_button_a_key_index].key_id]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_B]) && !previous_joystick_state.values[ODROID_INPUT_B]) {
        eventMap[msx_keyboard[msx_button_b_key_index].key_id]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_B]) && previous_joystick_state.values[ODROID_INPUT_B]) {
        eventMap[msx_keyboard[msx_button_b_key_index].key_id]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_START]) && !previous_joystick_state.values[ODROID_INPUT_START]) {
        eventMap[EC_F5]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_START]) && previous_joystick_state.values[ODROID_INPUT_START]) {
        eventMap[EC_F5]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_SELECT]) && !previous_joystick_state.values[ODROID_INPUT_SELECT]) {
        eventMap[EC_F4]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_SELECT]) && previous_joystick_state.values[ODROID_INPUT_SELECT]) {
        eventMap[EC_F4]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_X]) && !previous_joystick_state.values[ODROID_INPUT_X]) {
        eventMap[EC_F3]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_X]) && previous_joystick_state.values[ODROID_INPUT_X]) {
        eventMap[EC_F3]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_Y]) && !previous_joystick_state.values[ODROID_INPUT_Y]) {
        eventMap[EC_F2]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_Y]) && previous_joystick_state.values[ODROID_INPUT_Y]) {
        eventMap[EC_F2]  = 0;
    }

    // Handle keyboard emulation
    if (pressed_key) {
        eventMap[pressed_key] = 1;
        release_key = pressed_key;
        pressed_key = 0;
    } else if (release_key) {
        if (release_key_delay == 0) {
            eventMap[pressed_key] = 0;
            release_key = 0;
            release_key_delay = RELEASE_KEY_DELAY;
        } else {
            release_key_delay--;
        }
    }

    memcpy(&previous_joystick_state,joystick,sizeof(odroid_gamepad_state_t));
}

static void createOptionMenu(odroid_dialog_choice_t *options) {
    int index=0;
    if (strcmp(ROM_EXT,MSX_DISK_EXTENSION) == 0) {
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

static int underrun = 0;
static int overrun = 0;
static Machine msxMachine;
void app_main_msx(uint8_t load_state, uint8_t start_paused)
{
    int i;
    odroid_dialog_choice_t options[10];
    char game_name[80];

    createOptionMenu(options);

    memset(&msxMachine,0,sizeof(Machine));

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS_MSX + 0.5f);

    odroid_system_init(APPID_MSX, AUDIO_MSX_SAMPLE_RATE);
    odroid_system_emu_init(&msx_system_LoadState, &msx_system_SaveState, NULL);

    image_buffer_base_width    =  320;
    image_buffer_current_width =  image_buffer_base_width;
    image_buffer_height        =  240;

    properties = propCreate(1, EMU_LANG_ENGLISH, P_KBD_EUROPEAN, P_EMU_SYNCNONE, "");
    properties->sound.stereo = 0;
    properties->emulation.speed = FPS_MSX;
    properties->emulation.vdpSyncMode = P_VDP_SYNCAUTO;
    properties->emulation.enableFdcTiming = 0;
    properties->emulation.noSpriteLimits = 0;
    properties->video.fullscreen.width = 320;
    properties->video.fullscreen.width = 240;
    properties->sound.masterVolume = 100;

    // Default : enable SCC and disable MSX-MUSIC
    // This will be changed dynamicly if the game use MSX-MUSIC
    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_PSG].pan = 0;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].pan = 0;
    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].pan = 0;

    mixer = mixerCreate();

    emulatorInit(properties, mixer);
    actionInit(video, properties, mixer);

    emulatorRestartSound();

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++)
    {
        mixerSetChannelTypeVolume(mixer, i, properties->sound.mixerChannel[i].volume);
        mixerSetChannelTypePan(mixer, i, properties->sound.mixerChannel[i].pan);
        mixerEnableChannelType(mixer, i, properties->sound.mixerChannel[i].enable);
    }

    mixerSetMasterVolume(mixer, properties->sound.masterVolume);
    mixerEnableMaster(mixer, properties->sound.masterEnable);

    sprintf(game_name,"%s.%s",ACTIVE_FILE->name,ACTIVE_FILE->ext);
    if (0 == strcmp(ACTIVE_FILE->ext,MSX_DISK_EXTENSION)) {
        const rom_system_t *msx_system = rom_manager_system(&rom_mgr, "MSX");
        selected_disk_index = rom_get_index_for_file_ext(msx_system,ACTIVE_FILE);

        insertDiskette(properties, 0, game_name, NULL, -1);
        // We load SCC-I cartridge for disk games requiring it
        insertCartridge(properties, 0, CARTNAME_SNATCHER, NULL, ROM_SNATCHER, -1);
    } else {
        insertCartridge(properties, 0, game_name, NULL, ACTIVE_FILE->mapper, -1);
    }

    boardSetFdcTimingEnable(0/*properties->emulation.enableFdcTiming*/);
    boardSetY8950Enable(0/*properties->sound.chip.enableY8950*/);
    boardSetYm2413Enable(1/*properties->sound.chip.enableYM2413*/);
    boardSetMoonsoundEnable(0/*properties->sound.chip.enableMoonsound*/);
    boardSetVideoAutodetect(1/*properties->video.detectActiveMonitor*/);

    msxMachine.board.type = BOARD_MSX_T9769B;
    msxMachine.video.vdpVersion = VDP_V9958;
    msxMachine.video.vramSize = 128 * 1024;
    msxMachine.cmos.enable = 1;
    msxMachine.cpu.freqZ80 = 3579545;
    msxMachine.cpu.freqR800 = 7159090;
    msxMachine.fdc.count = 1;
    msxMachine.cmos.batteryBacked = 1;
    msxMachine.audio.psgstereo = 0;
    msxMachine.audio.psgpan[0] = 0;
    msxMachine.audio.psgpan[1] = -1;
    msxMachine.audio.psgpan[2] = 1;

    msxMachine.cpu.hasR800 = 0;
    msxMachine.fdc.enabled = 0;

    msxMachine.slot[0].subslotted = 1;
    msxMachine.slot[1].subslotted = 0;
    msxMachine.slot[2].subslotted = 1;
    msxMachine.slot[3].subslotted = 1;
    msxMachine.cart[0].slot = 1;
    msxMachine.cart[0].subslot = 0;
    msxMachine.cart[1].slot = 2;
    msxMachine.cart[1].subslot = 0;

    i=0;

    msxMachine.slotInfo[i].slot = 3;
    msxMachine.slotInfo[i].subslot = 0;
    msxMachine.slotInfo[i].startPage = 0;
    msxMachine.slotInfo[i].pageCount = 16; // 128kB of RAM
    msxMachine.slotInfo[i].romType = RAM_MAPPER;
    strcpy(msxMachine.slotInfo[i].name, "");
    i++;

    msxMachine.slotInfo[i].slot = 0;
    msxMachine.slotInfo[i].subslot = 0;
    msxMachine.slotInfo[i].startPage = 0;
    msxMachine.slotInfo[i].pageCount = 0;
    msxMachine.slotInfo[i].romType = ROM_F4INVERTED;
    strcpy(msxMachine.slotInfo[i].name, "");
    i++;

    msxMachine.slotInfo[i].slot = 0;
    msxMachine.slotInfo[i].subslot = 0;
    msxMachine.slotInfo[i].startPage = 0;
    msxMachine.slotInfo[i].pageCount = 4;
    msxMachine.slotInfo[i].romType = ROM_CASPATCH;
    strcpy(msxMachine.slotInfo[i].name, "MSX2P.rom");
    i++;

    msxMachine.slotInfo[i].slot = 3;
    msxMachine.slotInfo[i].subslot = 1;
    msxMachine.slotInfo[i].startPage = 0;
    msxMachine.slotInfo[i].pageCount = 2;
    msxMachine.slotInfo[i].romType = ROM_NORMAL;
    strcpy(msxMachine.slotInfo[i].name, "MSX2PEXT.rom");
    i++;

    msxMachine.slotInfo[i].slot = 3;
    msxMachine.slotInfo[i].subslot = 2;
    msxMachine.slotInfo[i].startPage = 2;
    msxMachine.slotInfo[i].pageCount = 4;
    msxMachine.slotInfo[i].romType = ROM_TC8566AF;
    strcpy(msxMachine.slotInfo[i].name, "PANASONICDISK.rom");
    i++;

    msxMachine.slotInfo[i].slot = 0;
    msxMachine.slotInfo[i].subslot = 2;
    msxMachine.slotInfo[i].startPage = 2;
    msxMachine.slotInfo[i].pageCount = 2;
    msxMachine.slotInfo[i].romType = ROM_MSXMUSIC; // FMPAC
    strcpy(msxMachine.slotInfo[i].name, "MSX2PMUS.rom");
    i++;

    msxMachine.slotInfoCount = i;

    memset(lcd_get_active_buffer(), 0, sizeof(framebuffer1));
    memset(lcd_get_inactive_buffer(), 0, sizeof(framebuffer1));

    // Init Sound
    msxAudioBufferOffset = 0;
    memset(audiobuffer_emulator, 0, sizeof(audiobuffer_emulator));
    memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
    HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audiobuffer_dma, AUDIO_BUFFER_LENGTH_DMA_MSX);

    emulatorStartMachine(NULL, &msxMachine);
    // Enable SCC and disable MSX-MUSIC as G&W is not powerfull enough to handle both at same time
    // If a game wants to play MSX-MUSIC sound, the mapper will detect it and it will disable SCC
    // and enbale MSX-MUSIC
    mixerEnableChannelType(boardGetMixer(), MIXER_CHANNEL_SCC, 1);
    mixerEnableChannelType(boardGetMixer(), MIXER_CHANNEL_MSXMUSIC, 0);

    while (1) {
        bool drawFrame = common_emu_frame_loop();
        wdog_refresh();
        odroid_gamepad_state_t joystick;
        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options);
        msxInputUpdate(&joystick);
        ((R800*)boardInfo.cpuRef)->terminate = 0;
        boardInfo.run(boardInfo.cpuRef);
        if (drawFrame) {
            common_ingame_overlay();
            lcd_swap();
        }
        size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : MSX_AUDIO_BUFFER_LENGTH;
        if (msxAudioBufferOffset >= MSX_AUDIO_BUFFER_LENGTH) {
#ifdef DEBUG_AUDIO
            printf("DMA copy offset %d\n",msxAudioBufferOffset);
#endif
            memcpy(&audiobuffer_dma[offset],audiobuffer_emulator,MSX_AUDIO_BUFFER_LENGTH*sizeof(Int16));
//            msxAudioBufferOffset = 0;
            msxAudioBufferOffset -= MSX_AUDIO_BUFFER_LENGTH;
            // Move second buffer to first location
            memcpy(audiobuffer_emulator,&audiobuffer_emulator[MSX_AUDIO_BUFFER_LENGTH],MSX_AUDIO_BUFFER_LENGTH*sizeof(Int16));
        } else {
#ifdef DEBUG_AUDIO
            printf("DMA underrun %d\n",underrun++);
#endif
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
}

static int8_t currentVolume = -1;
const uint8_t volume_table[ODROID_AUDIO_VOLUME_MAX + 1] = {
    0,
    6,
    12,
    19,
    25,
    35,
    46,
    60,
    80,
    100,
};

static Int32 soundWrite(void* dummy, Int16 *buffer, UInt32 count)
{
    uint8_t volume = odroid_audio_volume_get();
    int32_t factor = volume_tbl[volume];
    int32_t sample;
    if (volume != currentVolume) {
        if (volume == 0) {
            mixerSetEnable(mixer,0);
        } else {
            mixerSetEnable(mixer,1);
            mixerSetMasterVolume(mixer,volume_table[volume]);
        }
        currentVolume = volume;
    }
    if (msxAudioBufferOffset <= MSX_AUDIO_BUFFER_LENGTH) {
        memcpy(&audiobuffer_emulator[msxAudioBufferOffset],buffer,count*sizeof(Int16));
        msxAudioBufferOffset+=MSX_AUDIO_BUFFER_LENGTH;
    }
#ifdef DEBUG_AUDIO
    printf("soundWrite offset %d count %d\n",msxAudioBufferOffset,count);
#endif
#if 0
    for (int i = 0; i < count; i++) {
        sample = buffer[i];
        if (msxAudioBufferOffset >= MSX_AUDIO_BUFFER_LENGTH*2)
        {
#ifdef DEBUG_AUDIO
            printf("DMA overrun %d (count %d i %d)\n",overrun++, count, i);
#endif
            break;
        }
        if (audio_mute || volume == ODROID_AUDIO_VOLUME_MIN) {
                audiobuffer_emulator[msxAudioBufferOffset] = 0;
        } else {
                audiobuffer_emulator[msxAudioBufferOffset] = (sample * factor) >> 8;
        }
        msxAudioBufferOffset++;
    }
#endif
    return 0;
}

void archSoundCreate(Mixer* mixer, UInt32 sampleRate, UInt32 bufferSize, Int16 channels) {
    mixerSetStereo(mixer, 0);
    mixerSetWriteCallback(mixer, soundWrite, NULL, MSX_AUDIO_BUFFER_LENGTH);
}

void archSoundDestroy(void) {}
