#include "config.h"
#include "odroid_audio.h"
#include "gw_audio.h"

static int8_t volume = ODROID_AUDIO_VOLUME_MAX;

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
    (void)sample_rate;
}

void odroid_audio_set_sink(ODROID_AUDIO_SINK sink) { (void)sink; }

ODROID_AUDIO_SINK odroid_audio_get_sink(void)
{
    return ODROID_AUDIO_SINK_SPEAKER;
}

void odroid_audio_terminate(void) {}

void odroid_audio_submit(short *stereoAudioBuffer, int frameCount)
{
    (void)stereoAudioBuffer;
    (void)frameCount;
}

int odroid_audio_sample_rate_get(void)
{
    return 48000;
}

void odroid_audio_mute(bool mute)
{
    if (mute)
        audio_clear_buffers();
    audio_mute = mute;
}
