#include "gw_audio.h"

#include <string.h>
#include <stdbool.h>

uint32_t audio_mute;

int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2];
dma_transfer_state_t dma_state;
uint32_t dma_counter;

static uint16_t audiobuffer_full_length = AUDIO_BUFFER_LENGTH * 2;

uint16_t audio_get_buffer_full_length(void)
{
    return audiobuffer_full_length;
}

uint16_t audio_get_buffer_length(void)
{
    bool first = (dma_state == DMA_TRANSFER_STATE_HF);
    return first ? audiobuffer_full_length / 2 : (audiobuffer_full_length + 1) / 2;
}

uint16_t audio_get_buffer_size(void)
{
    return audio_get_buffer_length() * sizeof(int16_t);
}

int16_t *audio_get_active_buffer(void)
{
    size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : audiobuffer_full_length / 2;
    return &audiobuffer_dma[offset];
}

int16_t *audio_get_inactive_buffer(void)
{
    size_t offset = (dma_state == DMA_TRANSFER_STATE_TC) ? 0 : audiobuffer_full_length / 2;
    return &audiobuffer_dma[offset];
}

void audio_clear_active_buffer(void)
{
    bool first = (dma_state == DMA_TRANSFER_STATE_HF);
    size_t n = first ? audiobuffer_full_length / 2 : (audiobuffer_full_length + 1) / 2;
    memset(audio_get_active_buffer(), 0, n * sizeof(int16_t));
}

void audio_clear_inactive_buffer(void)
{
    bool first = (dma_state == DMA_TRANSFER_STATE_HF);
    size_t n = first ? (audiobuffer_full_length + 1) / 2 : audiobuffer_full_length / 2;
    memset(audio_get_inactive_buffer(), 0, n * sizeof(int16_t));
}

void audio_clear_buffers(void)
{
    memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
}

void audio_set_buffer_length(uint16_t length)
{
    (void)length;
}

void audio_start_playing(uint16_t length)
{
    audio_start_playing_full_length((uint16_t)(length * 2));
}

void audio_start_playing_full_length(uint16_t full_length)
{
    audio_clear_buffers();
    audiobuffer_full_length = full_length;
    dma_state = DMA_TRANSFER_STATE_HF;
}

void audio_stop_playing(void)
{
    audio_clear_buffers();
}
