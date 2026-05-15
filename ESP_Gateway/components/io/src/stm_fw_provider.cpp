// stm_fw_provider.cpp
#include "io/stm_fw_provider.h"
#include "io/stm_fw_image_raw.h"
#include "io/stm_fw_storage.h"

bool stmFwGetEmbeddedTest(StmFirmwareImage& img)
{
    img.data = stm_fw_data;
    img.size = stm_fw_size;
    img.source = StmFwSource::EmbeddedTest;
    return img.data && img.size > 0;
}

bool stmFwGetKnownGood(std::vector<uint8_t>& fw)
{
    return stmFwStorageReadKnownGood(fw);
}