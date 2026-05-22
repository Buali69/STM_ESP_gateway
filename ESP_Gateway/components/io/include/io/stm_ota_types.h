#pragma once

#include <stdint.h>

enum class StmOtaRequestSource : uint8_t {
    DevEmbedded = 0,
    Server      = 1,
    Manual      = 2,
    Rollback    = 3
};

struct StmOtaContext {
    StmOtaRequestSource source;

    uint32_t fwVersion;

    uint64_t jobId;

    bool rollback;

    bool signatureVerified;
};