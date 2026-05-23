#include "io/stm_fw_manifest.h"
#include <string.h>
#include "psa/crypto.h"
#include "esp_log.h"

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

static void writeU32Le(uint8_t* out, size_t& pos, uint32_t v)
{
    out[pos++] = static_cast<uint8_t>(v & 0xFF);
    out[pos++] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[pos++] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[pos++] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

bool stmFwManifestComputeSignedHash(
    const StmFwManifest& manifest,
    uint8_t outSha256[32]
)
{
    if (!outSha256) {
        return false;
    }

    uint8_t buf[4 + 4 + 4 + 4 + 4 + 32 + 4 + 4];
    size_t pos = 0;

    writeU32Le(buf, pos, manifest.magic);
    writeU32Le(buf, pos, manifest.manifestVersion);
    writeU32Le(buf, pos, manifest.fwVersion);
    writeU32Le(buf, pos, manifest.minBootloaderVersion);
    writeU32Le(buf, pos, manifest.fwSize);
    writeU32Le(buf, pos, manifest.flags);
    writeU32Le(buf, pos, manifest.signatureAlg);

    memcpy(&buf[pos], manifest.sha256, 32);
    pos += 32;

    ESP_LOGI("stm_fw_manifest",
         "VERIFY signedBytes len=%u",
         (unsigned)pos);

    ESP_LOG_BUFFER_HEX_LEVEL(
        "stm_fw_manifest",
        buf,
        pos,
        ESP_LOG_INFO);

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
        buf,
        pos,
        outSha256,
        32,
        &hashLen
    );

    return st == PSA_SUCCESS && hashLen == 32;
}