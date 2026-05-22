#include "io/stm_fw_signature.h"
#include "io/stm_fw_trust.h"

#include "esp_log.h"
#include "psa/crypto.h"

static const char* TAG = "stm_fw_signature";

bool stmFwSignatureVerifyManifest(const StmFwManifest& manifest)
{
    if (!stmFwManifestHasSignature(manifest)) {
        ESP_LOGW(TAG, "manifest has no signature");
        return false;
    }

    uint8_t signedHash[32];

    if (!stmFwManifestComputeSignedHash(manifest, signedHash)) {
        ESP_LOGE(TAG, "failed to compute signed manifest hash");
        return false;
    }

    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %ld", (long)st);
        return false;
    }

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(
        &attr,
        PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1)
    );

    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

    psa_key_id_t key = 0;

    st = psa_import_key(
        &attr,
        g_stmFwRootPublicKey,
        g_stmFwRootPublicKeySize,
        &key
    );

    psa_reset_key_attributes(&attr);

    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_import_key failed: %ld", (long)st);
        return false;
    }

    st = psa_verify_hash(
        key,
        PSA_ALG_ECDSA(PSA_ALG_SHA_256),
        signedHash,
        sizeof(signedHash),
        manifest.signature,
        sizeof(manifest.signature)
    );

    psa_destroy_key(key);

    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "ECDSA manifest signature invalid: %ld", (long)st);
        return false;
    }

    ESP_LOGI(TAG, "ECDSA manifest signature valid");
    return true;
}