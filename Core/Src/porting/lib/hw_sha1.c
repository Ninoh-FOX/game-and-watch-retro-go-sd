#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "hw_sha1.h"
#include "main.h"
#include "stm32h7xx_hal.h"

int8_t calculate_sha1_hw(const uint8_t *data, size_t len, uint8_t *output) {
    __HAL_RCC_HASH_CLK_ENABLE();

    HASH_HandleTypeDef hhash;

    hhash.Init.DataType = HASH_DATATYPE_8B;
    if (HAL_HASH_Init(&hhash) != HAL_OK) {
        return 0;
    }

    if (HAL_HASH_SHA1_Start(&hhash, (uint8_t*)data, len, output, HAL_MAX_DELAY) != HAL_OK) {
        return 0;
    }

    // Libérer le périphérique HASH
    HAL_HASH_DeInit(&hhash);

    __HAL_RCC_HASH_CLK_DISABLE();
    return 1;
}
