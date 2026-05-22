// stm_fw_provider.cpp
#include "io/stm_fw_provider.h"
#include "io/stm_fw_image_raw.h"
#include "io/stm_fw_storage.h"
#include "io/stm_fw_manifest.h"
#include "psa/crypto.h"
#include <string.h>

const uint8_t stm_fw_signature[64] = {
    0x51, 0x57, 0x7f, 0xf7, 0x7c, 0x74, 0xa1, 0x9a,
    0xd6, 0x18, 0x26, 0xe9, 0xbf, 0xba, 0xeb, 0xc6,
    0xbf, 0x39, 0x33, 0x9d, 0xd3, 0x8a, 0x92, 0xa9,
    0x69, 0xcc, 0x2d, 0x4a, 0x49, 0x57, 0x98, 0x86,
    0x88, 0x0d, 0x98, 0x6b, 0x09, 0x11, 0x4d, 0xc6,
    0x72, 0xe7, 0x8e, 0x44, 0xf9, 0x28, 0x0a, 0x4a,
    0x70, 0xa7, 0x3c, 0x85, 0x71, 0x54, 0xc3, 0xee,
    0xb3, 0x57, 0xcb, 0x49, 0x43, 0x7a, 0xec, 0x8c,
};

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
    //img.manifest.flags = 0;
    img.manifest.flags = STM_FW_FLAG_SIGNATURE_REQUIRED;
    //img.manifest.signatureAlg = STM_FW_SIG_NONE;
    img.manifest.signatureAlg = STM_FW_SIG_ECDSA_P256_SHA256;
    memcpy(img.manifest.signature, stm_fw_signature, sizeof(stm_fw_signature));
    //img.manifest.signature[0] = 0x01;

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