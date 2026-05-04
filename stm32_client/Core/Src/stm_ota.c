#include "stm_ota.h"

static stm_ota_state_t state = STM_OTA_IDLE;
static uint32_t expected_size = 0;
static uint32_t expected_crc32 = 0;
static uint32_t received_size = 0;
static uint32_t running_crc32 = 0;

void stm_ota_init(void)
{
    stm_ota_reset();
}

void stm_ota_reset(void)
{
    state = STM_OTA_IDLE;
    expected_size = 0;
    expected_crc32 = 0;
    received_size = 0;
    running_crc32 = 0;
}

int stm_ota_prepare(uint32_t size, uint32_t crc32)
{
    if (size == 0) {
        state = STM_OTA_ERROR;
        return -1;
    }

    stm_ota_crc_reset();
    expected_size = size;
    expected_crc32 = crc32;
    received_size = 0;      
    state = STM_OTA_READY;
    return 0;
}

int stm_ota_data_begin(void)
{
    if (state != STM_OTA_READY) {
        state = STM_OTA_ERROR;
        return -1;
    }

    state = STM_OTA_RECEIVING;
    return 0;
}

int stm_ota_finish(void)
{
    if (state != STM_OTA_RECEIVING && state != STM_OTA_READY) {
        state = STM_OTA_ERROR;
        return -1;
    }

    state = STM_OTA_DONE;
    return 0;
}

int stm_ota_abort(void)
{
    stm_ota_reset();
    return 0;
}

stm_ota_state_t stm_ota_get_state(void)
{
    return state;
}

uint32_t stm_ota_get_expected_size(void)
{
    return expected_size;
}

uint32_t stm_ota_get_expected_crc32(void)
{
    return expected_crc32;
}

int stm_ota_add_received(uint32_t len)
{
    received_size += len;

    if (received_size > expected_size) {
        state = STM_OTA_ERROR;
        return -1;
    }

    return 0;
}

uint32_t stm_ota_get_received_size(void)
{
    return received_size;
}

static uint32_t crc32_le_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    while (len--) {
        crc ^= (uint32_t)(*data++);

        for (uint32_t i = 0; i < 8; i++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

void stm_ota_crc_reset(void)
{
    running_crc32 = UINT32_MAX;
}

void stm_ota_crc_update(const uint8_t *data, uint32_t len)
{
    running_crc32 = crc32_le_update(running_crc32, data, len);
}

uint32_t stm_ota_get_final_crc32(void)
{
    return running_crc32 ^ UINT32_MAX;
}