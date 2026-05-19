// stm_fw_provider.cpp
#include "io/stm_fw_provider.h"
#include "io/stm_fw_image_raw.h"
#include "io/stm_fw_storage.h"
#include "io/stm_fw_manifest.h"
#include "psa/crypto.h"
#include <string.h>

static bool calcSha256(const uint8_t* data, size_t size, uint8_t out[32])
{
    if (!data || size == 0 || !out) {
        return false;
    }

    static bool psaReady = false;

    if (!psaReady) {
        psa_status_t st = psa_crypto_init();
        if (st != PSA_SUCCESS) {
            return false;
        }
        psaReady = true;
    }

    size_t hashLen = 0;

    psa_status_t st = psa_hash_compute(
        PSA_ALG_SHA_256,
        data,
        size,
        out,
        32,
        &hashLen
    );

    return st == PSA_SUCCESS && hashLen == 32;
}

bool stmFwGetEmbeddedTest(StmFirmwareImage& img)
{
    img.data = stm_fw_data;
    img.size = stm_fw_size;
    img.source = StmFwSource::EmbeddedTest;

    if (!img.data || img.size == 0) {
        return false;
    }

    memset(&img.manifest, 0, sizeof(img.manifest));
    memset(img.manifest.signature, 0, sizeof(img.manifest.signature));

    img.manifest.magic = STM_FW_MANIFEST_MAGIC;
    img.manifest.manifestVersion = 1;
    img.manifest.fwVersion = 1;
    img.manifest.minBootloaderVersion = 1;
    img.manifest.fwSize = img.size;
    img.manifest.flags = 0;

    if (!calcSha256(img.data, img.size, img.manifest.sha256)) {
        return false;
    }

    return true;
}

bool stmFwGetKnownGood(std::vector<uint8_t>& fw)
{
    return stmFwStorageReadKnownGood(fw);
}

bool stmFwManifestHasSignature(const StmFwManifest& m)
{
    for (size_t i = 0; i < sizeof(m.signature); ++i) {
        if (m.signature[i] != 0) {
            return true;
        }
    }

    return false;
}