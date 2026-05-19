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

    uint8_t signature[64];
};

extern const uint32_t STM_FW_MANIFEST_MAGIC;

static constexpr uint32_t STM_FW_FLAG_SIGNATURE_REQUIRED = 0x00000001;

bool stmFwManifestIsValid(const StmFwManifest& m);

bool stmFwManifestSha256Matches(const StmFwManifest& m, const uint8_t* data, uint32_t size);
bool stmFwManifestComputeSignedHash(const StmFwManifest& manifest, uint8_t outSha256[32]);

bool stmFwManifestRequiresSignature(const StmFwManifest& m);
bool stmFwManifestHasSignature(const StmFwManifest& m);


