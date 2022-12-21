#include "build/config.h"
#ifdef ENABLE_EMULATOR_AMSTRAD

#include <odroid_system.h>

#include "main.h"
#include "appid.h"

#include "common.h"
#include "gw_linker.h"
#include "gw_flash.h"
#include "gw_lcd.h"
#include "main_amstrad.h"
#include "save_amstrad.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *headerString = "AMST0000";

#define WORK_BLOCK_SIZE (256)
static int flashBlockOffset = 0;
static bool isLastFlashWrite = 0;

extern int cap32_save_state();
extern int cap32_load_state();

// This function fills 4kB blocks and writes them in flash when full
static void SaveFlashSaveData(uint8_t *dest, uint8_t *src, int size) {
    int blockNumber = 0;
    for (int i = 0; i < size; i++) {
        // We use amstrad_framebuffer as a temporary buffer
        amstrad_framebuffer[flashBlockOffset] = src[i];
        flashBlockOffset++;
        if ((flashBlockOffset == WORK_BLOCK_SIZE) || (isLastFlashWrite && (i == size-1))) {
            // Write block in flash
            int intDest = (int)dest+blockNumber*WORK_BLOCK_SIZE;
            intDest = intDest & ~(WORK_BLOCK_SIZE-1);
            unsigned char *newDest = (unsigned char *)intDest;
            OSPI_DisableMemoryMappedMode();
            OSPI_Program((uint32_t)newDest,(const uint8_t *)amstrad_framebuffer,WORK_BLOCK_SIZE);
            OSPI_EnableMemoryMappedMode();
            flashBlockOffset = 0;
            blockNumber++;
        }
    }
    if (size == 0) {
        // force writing last data block
        int intDest = (int)dest+blockNumber*WORK_BLOCK_SIZE;
        intDest = intDest & ~(WORK_BLOCK_SIZE-1);
        unsigned char *newDest = (unsigned char *)intDest;
        OSPI_DisableMemoryMappedMode();
        OSPI_Program((uint32_t)newDest,(const uint8_t *)amstrad_framebuffer,WORK_BLOCK_SIZE);
        OSPI_EnableMemoryMappedMode();
        flashBlockOffset = 0;
        blockNumber++;
    }
}

static SaveState amstradSaveState;

static uint32_t tagFromName(const char* tagName)
{
    uint32_t tag = 0;
    uint32_t mod = 1;

    while (*tagName) {
        mod *= 19219;
        tag += mod * *tagName++;
    }

    return tag;
}

/* Savestate functions */
uint32_t saveAmstradState(uint8_t *destBuffer, uint32_t save_size) {
    // Convert mem mapped pointer to flash address
    uint32_t save_address = destBuffer - &__EXTFLASH_BASE__;
    // Fill context data
    amstradSaveState.buffer = (uint8_t *)save_address;
    amstradSaveState.offset = 0;
    amstradSaveState.section = 0;

    // Erase flash memory
    store_erase((const uint8_t *)destBuffer, save_size);

    isLastFlashWrite = 0;
    // Reserve a page of 256 Bytes for the header info
    // We put 0xff to be able to update values at the end of process
    SaveFlashSaveData(amstradSaveState.buffer,(uint8_t *)headerString,8);
    memset(amstradSaveState.sections,0xff,sizeof(amstradSaveState.sections[0])*MAX_SECTIONS);
    SaveFlashSaveData(amstradSaveState.buffer+8,(uint8_t *)amstradSaveState.sections,sizeof(amstradSaveState.sections[0])*MAX_SECTIONS);
    amstradSaveState.offset += 8+sizeof(amstradSaveState.sections[0])*MAX_SECTIONS;
    // Start saving data
    cap32_save_state();
//    save_gnw_amstrad_data();

    // Write dummy data to force writing last block of data
    SaveFlashSaveData(amstradSaveState.buffer+amstradSaveState.offset,NULL,0);

    // Copy header data in the first 256 bytes block of flash
    SaveFlashSaveData(amstradSaveState.buffer,(uint8_t *)headerString,8);
    isLastFlashWrite = true;
    SaveFlashSaveData(amstradSaveState.buffer+8,(uint8_t *)amstradSaveState.sections,sizeof(amstradSaveState.sections[0])*MAX_SECTIONS);

    return amstradSaveState.offset;
}

void amstradSaveStateSet(SaveState* state, const char* tagName, uint32_t value)
{
    SaveFlashSaveData(state->buffer+state->offset,(uint8_t *)&value,sizeof(uint32_t));
    state->offset+=sizeof(uint32_t);
}

void amstradSaveStateSetBuffer(SaveState* state, const char* tagName, void* buffer, uint32_t length)
{
    SaveFlashSaveData(state->buffer + state->offset, (uint8_t *)buffer, length);
    state->offset += length;
}

SaveState* amstradSaveStateOpenForWrite(const char* fileName)
{
    // Update section
    amstradSaveState.sections[amstradSaveState.section].tag = tagFromName(fileName);
    amstradSaveState.sections[amstradSaveState.section].offset = amstradSaveState.offset;
    amstradSaveState.section++;
    return &amstradSaveState;
}

/* Loadstate functions */
uint32_t loadAmstradState(uint8_t *srcBuffer) {
    amstradSaveState.offset = 0;
    // Check for header
    if (memcmp(headerString,srcBuffer,8) == 0) {
        amstradSaveState.buffer = srcBuffer;
        // Copy sections header in structure
        memcpy(amstradSaveState.sections,amstradSaveState.buffer+8,sizeof(amstradSaveState.sections[0])*MAX_SECTIONS);
        cap32_load_state();

//        load_gnw_amstrad_data(srcBuffer);
    }
    return amstradSaveState.offset;
}

SaveState* amstradSaveStateOpenForRead(const char* fileName)
{
    // find offset
    uint32_t tag = tagFromName(fileName);
    for (int i = 0; i<MAX_SECTIONS; i++) {
        if (amstradSaveState.sections[i].tag == tag) {
            // Found tag
            amstradSaveState.offset = amstradSaveState.sections[i].offset;
            return &amstradSaveState;
        }
    }

    return NULL;
}

uint32_t amstradSaveStateGet(SaveState* state, const char* tagName)
{
    uint32_t value;

    memcpy(&value, state->buffer + state->offset, sizeof(uint32_t));
    state->offset+=sizeof(uint32_t);
    return value;
}

void amstradSaveStateGetBuffer(SaveState* state, const char* tagName, void* buffer, uint32_t length)
{
    memcpy(buffer, state->buffer + state->offset, length);
    state->offset += length;
}

#endif