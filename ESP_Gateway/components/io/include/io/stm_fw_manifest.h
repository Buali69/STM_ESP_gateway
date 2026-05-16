#pragma once

#include <stdint.h>

struct StmFwManifest {
    uint32_t magic;
    uint32_t manifestVersion;

    uint32_t fwVersion;
    uint32_t minBootloaderVersion;

    uint32_t fwSize;

    uint8_t sha256[32];

    uint32_t flags;
};

bool stmFwManifestIsValid(const StmFwManifest& m);

bool stmFwManifestSha256Matches(
    const StmFwManifest& m,
    const uint8_t* data,
    uint32_t size
);

extern const uint32_t STM_FW_MANIFEST_MAGIC;