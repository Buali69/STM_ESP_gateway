#include "io/stm_fw_signature.h"
#include "io/stm_fw_trust.h"

#include "esp_log.h"

static const char* TAG = "stm_fw_signature";

bool stmFwSignatureVerifyManifest(const StmFwManifest& manifest)
{
    if (!stmFwManifestHasSignature(manifest)) {
        ESP_LOGW(TAG, "manifest has no signature");
        return false;
    }

    ESP_LOGI(TAG,
         "trust anchor loaded: %u bytes",
         (unsigned)g_stmFwRootPublicKeySize);

    // TODO: verify signature over canonical manifest fields.
    // Later: public key / secure element / server trust anchor.

    uint8_t signedHash[32];

    if (!stmFwManifestComputeSignedHash(manifest, signedHash)) {
        ESP_LOGE(TAG, "failed to compute signed manifest hash");
        return false;
    }

    ESP_LOGI(TAG, "signed manifest hash computed");
    return false;
}