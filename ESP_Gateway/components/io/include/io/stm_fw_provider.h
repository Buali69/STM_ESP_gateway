// stm_fw_provider.h
#pragma once
#include <cstdint>
#include <vector>

enum class StmFwSource {
    None,
    EmbeddedTest,
    FlashKnownGood,
    Downloaded
};

struct StmFirmwareImage {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    StmFwSource source = StmFwSource::None;
};

bool stmFwGetEmbeddedTest(StmFirmwareImage& img);
bool stmFwGetKnownGood(std::vector<uint8_t>& fw);