#pragma once

#include <stdint.h>
#include <stddef.h>

typedef enum {
    STM_OTA_IDLE = 0,
    STM_OTA_PREPARE,
    STM_OTA_READY,
    STM_OTA_RECEIVING,
    STM_OTA_VERIFYING,
    STM_OTA_DONE,
    STM_OTA_ERROR
} stm_ota_state_t;

void stm_ota_init(void);
void stm_ota_reset(void);

int stm_ota_prepare(uint32_t size, uint32_t crc32);
int stm_ota_data_begin(void);
int stm_ota_finish(void);
int stm_ota_abort(void);

stm_ota_state_t stm_ota_get_state(void);
uint32_t stm_ota_get_expected_size(void);
uint32_t stm_ota_get_expected_crc32(void);

int stm_ota_add_received(uint32_t len);
uint32_t stm_ota_get_received_size(void);

void stm_ota_crc_reset(void);
void stm_ota_crc_update(const uint8_t *data, uint32_t len);
uint32_t stm_ota_get_final_crc32(void);