#include "common_linux.h"

#include <SDL.h>
#include <stdint.h>
#include <string.h>

#include "gw_audio.h"
#include "odroid_audio.h"

#include "odroid_settings.h"
#include "rg_i18n.h"

const uint8_t volume_tbl[ODROID_AUDIO_VOLUME_MAX + 1] = {
    (uint8_t)(UINT8_MAX * 0.00f),
    (uint8_t)(UINT8_MAX * 0.06f),
    (uint8_t)(UINT8_MAX * 0.125f),
    (uint8_t)(UINT8_MAX * 0.187f),
    (uint8_t)(UINT8_MAX * 0.25f),
    (uint8_t)(UINT8_MAX * 0.35f),
    (uint8_t)(UINT8_MAX * 0.42f),
    (uint8_t)(UINT8_MAX * 0.60f),
    (uint8_t)(UINT8_MAX * 0.80f),
    (uint8_t)(UINT8_MAX * 1.00f),
};

cpumon_stats_t cpumon_stats;

common_emu_state_t common_emu_state = {
    .frame_time_10us = (uint16_t)(100000 / 60 + 0.5f),
    .clear_frames = 2,
};

static uint32_t dwt_start_ticks;

void common_emu_frame_loop_reset(void)
{
    memset(&common_emu_state, 0, sizeof(common_emu_state));
    common_emu_state.frame_time_10us = (uint16_t)(100000 / 60 + 0.5f);
    common_emu_state.clear_frames = 2;
}

bool common_emu_frame_loop(void)
{
    common_emu_state.last_sync_time = get_elapsed_time();
    return true;
}

void common_emu_input_loop(odroid_gamepad_state_t *joystick, odroid_dialog_choice_t *game_options, void_callback_t repaint)
{
    (void)joystick;
    (void)game_options;
    (void)repaint;
    /* Pause / menu: use desktop build of odroid_overlay (stubs) — no in-game menu on Linux */
}

void common_emu_input_loop_handle_turbo(odroid_gamepad_state_t *joystick)
{
    uint8_t turbo_buttons = odroid_settings_turbo_buttons_get();
    bool turbo_a = (joystick->values[ODROID_INPUT_A] && (turbo_buttons & 1));
    bool turbo_b = (joystick->values[ODROID_INPUT_B] && (turbo_buttons & 2));
    bool turbo_button = odroid_button_turbos();
    if (turbo_a)
        joystick->values[ODROID_INPUT_A] = turbo_button;
    if (turbo_b)
        joystick->values[ODROID_INPUT_B] = !turbo_button;
}

void common_emu_sound_sync(bool use_nops)
{
    (void)use_nops;
    /* Frame pacing (~60 Hz); avoids busy-wait on dma_counter */
    SDL_Delay(16);
}

bool common_emu_sound_loop_is_muted(void)
{
    if (audio_mute || odroid_audio_volume_get() == ODROID_AUDIO_VOLUME_MIN) {
        audio_clear_active_buffer();
        return true;
    }
    return false;
}

uint8_t common_emu_sound_get_volume(void)
{
    return volume_tbl[odroid_audio_volume_get()];
}

void common_emu_enable_dwt_cycles(void)
{
    dwt_start_ticks = SDL_GetTicks();
}

unsigned int common_emu_get_dwt_cycles(void)
{
    return (unsigned int)(SDL_GetTicks() - dwt_start_ticks);
}

void common_emu_clear_dwt_cycles(void)
{
    dwt_start_ticks = SDL_GetTicks();
}

void common_ingame_overlay(void) {}

void cpumon_busy(void) {}
void cpumon_sleep(void) {}
void cpumon_reset(void) {}
