#include "io/stm_fw_manifest.h"
#include <string.h>
#include "psa/crypto.h"

constexpr uint32_t STM_FW_MANIFEST_MAGIC = 0x53544D46; // STMF

bool stmFwManifestIsValid(const StmFwManifest& m)
{
    if (m.magic != STM_FW_MANIFEST_MAGIC) {
        return false;
    }

    if (m.manifestVersion != 1) {
        return false;
    }

    if (m.fwSize == 0) {
        return false;
    }

    return true;
}

bool stmFwManifestSha256Matches(
    const StmFwManifest& m,
    const uint8_t* data,
    uint32_t size
)
{
    if (!data || size == 0) {
        return false;
    }

    if (m.fwSize != size) {
        return false;
    }

    uint8_t calc[32];

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
        calc,
        sizeof(calc),
        &hashLen
    );

    if (st != PSA_SUCCESS || hashLen != sizeof(calc)) {
        return false;
    }

    return memcmp(calc, m.sha256, sizeof(calc)) == 0;
}

bool stmFwManifestRequiresSignature(const StmFwManifest& m)
{
    return (m.flags & STM_FW_FLAG_SIGNATURE_REQUIRED) != 0;
}