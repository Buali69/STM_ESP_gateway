#include "stm_ota.h"

static stm_ota_state_t state = STM_OTA_IDLE;
static uint32_t expected_size = 0;
static uint32_t expected_crc32 = 0;

void stm_ota_init(void)
{
    stm_ota_reset();
}

void stm_ota_reset(void)
{
    state = STM_OTA_IDLE;
    expected_size = 0;
    expected_crc32 = 0;
}

int stm_ota_prepare(uint32_t size, uint32_t crc32)
{
    if (size == 0) {
        state = STM_OTA_ERROR;
        return -1;
    }

    expected_size = size;
    expected_crc32 = crc32;
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