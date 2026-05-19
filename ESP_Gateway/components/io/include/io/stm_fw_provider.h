// stm_fw_provider.h
#pragma once
#include <cstdint>
#include <vector>

#include "io/stm_fw_manifest.h"

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
    StmFwManifest manifest;
};

bool stmFwGetEmbeddedTest(StmFirmwareImage& img);
bool stmFwGetKnownGood(std::vector<uint8_t>& fw);

bool stmFwManifestHasSignature(const StmFwManifest& m);