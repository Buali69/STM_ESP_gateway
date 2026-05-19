#pragma once

#include "io/stm_fw_manifest.h"

#include <cstdint>

struct StmFwPolicyResult {
    bool accepted = false;
    const char* reason = nullptr;
};

StmFwPolicyResult stmFwPolicyAcceptCandidate(
    const StmFwManifest& manifest,
    const uint8_t* data,
    uint32_t size,
    uint32_t currentVersion
);