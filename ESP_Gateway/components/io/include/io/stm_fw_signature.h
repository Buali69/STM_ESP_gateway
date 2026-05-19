#pragma once

#include "io/stm_fw_manifest.h"

#include <cstdint>

bool stmFwSignatureVerifyManifest(const StmFwManifest& manifest);