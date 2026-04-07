#include "config.h"
#include "odroid_audio.h"
#include "gw_audio.h"
#include <SDL.h>

static int8_t volume = ODROID_AUDIO_VOLUME_MAX;
static SDL_AudioDeviceID audio_device;
static int audio_sample_rate = 48000;

uint32_t odroid_audio_get_queued_size_bytes(void)
{
    if (audio_device == 0)
        return 0;
    return SDL_GetQueuedAudioSize(audio_device);
}

int odroid_audio_volume_get(void)
{
    return volume;
}

void odroid_audio_volume_set(int level)
{
    volume = (int8_t)level;
}

void odroid_audio_init(int sample_rate)
{
    SDL_AudioSpec wanted = {0};
    SDL_AudioSpec obtained = {0};

    audio_sample_rate = sample_rate > 0 ? sample_rate : 48000;
    if (audio_device != 0)
        return;

    wanted.freq = audio_sample_rate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 1;
    wanted.samples = 1024;
    wanted.callback = NULL;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audio_device == 0) {
        SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }

    audio_sample_rate = obtained.freq;
    SDL_PauseAudioDevice(audio_device, 0);
}

void odroid_audio_set_sink(ODROID_AUDIO_SINK sink) { (void)sink; }

ODROID_AUDIO_SINK odroid_audio_get_sink(void)
{
    return ODROID_AUDIO_SINK_SPEAKER;
}

void odroid_audio_terminate(void)
{
    if (audio_device != 0) {
        SDL_ClearQueuedAudio(audio_device);
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
}

void odroid_audio_submit(short *stereoAudioBuffer, int frameCount)
{
    if (audio_device == 0 || stereoAudioBuffer == NULL || frameCount <= 0)
        return;

    const uint32_t bytes_per_sample = sizeof(int16_t);
    SDL_QueueAudio(audio_device, stereoAudioBuffer, (uint32_t)frameCount * bytes_per_sample);
}

int odroid_audio_sample_rate_get(void)
{
    return audio_sample_rate;
}

void odroid_audio_mute(bool mute)
{
    if (mute)
        audio_clear_buffers();
    audio_mute = mute;
}
