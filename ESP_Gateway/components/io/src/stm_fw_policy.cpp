#include "io/stm_fw_policy.h"

StmFwPolicyResult stmFwPolicyAcceptCandidate(
    const StmFwManifest& manifest,
    const uint8_t* data,
    uint32_t size,
    uint32_t currentVersion
)
{
    if (!stmFwManifestIsValid(manifest)) {
        return {false, "invalid manifest"};
    }

    if (manifest.fwSize != size) {
        return {false, "firmware size mismatch"};
    }

    if (!stmFwManifestSha256Matches(manifest, data, size)) {
        return {false, "sha256 mismatch"};
    }

    if (manifest.fwVersion < currentVersion) {
        return {false, "version rollback blocked"};
    }

    if (stmFwManifestRequiresSignature(manifest) &&
        !stmFwManifestHasSignature(manifest)) {
        return {false, "signature required but missing"};
    }

    return {true, nullptr};
}