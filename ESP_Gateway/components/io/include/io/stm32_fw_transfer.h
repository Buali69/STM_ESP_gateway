#pragma once

#include <cstddef>
#include <cstdint>

bool stm32FwTransferBegin(uint32_t size, uint32_t crc32);
bool stm32FwTransferDataBegin();

bool stm32FwTransferSendDummyChunk(uint32_t seq);
bool stm32FwTransferSendDummyChunks(uint32_t count);

bool stm32FwTransferDataEnd(uint32_t seq);
bool stm32FwTransferEnd();

bool stm32FwTransferSendBuffer(const uint8_t* data, uint32_t size);
uint32_t stm32FwTransferCalcCrc32(const uint8_t* data, uint32_t len);