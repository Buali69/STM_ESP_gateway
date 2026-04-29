#pragma once

#include <string>
#include "io/ota_client.h"

enum class OtaMgrEvent {
    None,
    ConfirmOk,
    ConfirmTryFailed,
    OtaJobReady,
};



void otaMgrBegin(const std::string& fwVersion, const std::string& bootId);
bool otaMgrIsConfirmed();

// neu:
OtaMgrEvent otaMgrPoll(bool wifiOk, bool timeOk, bool forceTick, OtaJob* outJob);