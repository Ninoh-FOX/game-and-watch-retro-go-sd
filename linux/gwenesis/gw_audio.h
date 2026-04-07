/* Replaces Core/Inc/gw_audio.h (no SAI/DMA types) for Linux builds */
#pragma once

#include <stdint.h>

#define AUDIO_SAMPLE_RATE   (48000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50)

extern uint32_t audio_mute;

typedef enum {
    DMA_TRANSFER_STATE_HF = 0x00,
    DMA_TRANSFER_STATE_TC = 0x01,
} dma_transfer_state_t;

extern dma_transfer_state_t dma_state;
extern uint32_t dma_counter;

uint16_t audio_get_buffer_full_length(void);
uint16_t audio_get_buffer_length(void);
uint16_t audio_get_buffer_size(void);
int16_t *audio_get_active_buffer(void);
int16_t *audio_get_inactive_buffer(void);
void audio_clear_active_buffer(void);
void audio_clear_inactive_buffer(void);
void audio_clear_buffers(void);
void audio_set_buffer_length(uint16_t length);
void audio_start_playing(uint16_t length);
void audio_start_playing_full_length(uint16_t length);
void audio_stop_playing(void);
