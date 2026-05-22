#pragma once

#include <cstdint>
#include <string>


enum class ConfirmFwResult {
    Confirmed,
    NoPendingJob,
    RetryableError
};

struct OtaJob {
    uint64_t job_id = 0;
    uint64_t file_id = 0;
    std::string name;
    int64_t size = 0;
    std::string sha256hex;
    std::string url;
    std::string signatureBase64;
    bool isStm = false;
    uint32_t stmFwVersion = 0;
};

enum class OtaError {
    None,
    NoUpdatePartition,
    DownloadHttpFailed,
    DownloadTruncated,
    ShaBeginFailed,
    ShaUpdateFailed,
    ShaFinishFailed,
    Sha256Mismatch,
    SignatureMissing,
    SignatureInvalid,
    OtaBeginFailed,
    OtaWriteFailed,
    OtaEndFailed,
    SetBootPartitionFailed
};

bool pollOta(OtaJob& out);
bool runOtaJob(const OtaJob& job);
ConfirmFwResult confirmFwOncePerFw(const std::string& fw, const std::string& bootId);