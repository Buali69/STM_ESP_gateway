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

OtaMgrEvent otaMgrPoll(bool wifiOk, bool timeOk, bool forceTick, OtaJob* outJob);

bool otaMgrIsRunning();
void otaMgrSetRunning(bool running);

bool otaMgrServerKnown();
bool otaMgrServerOk();
void otaMgrSetServerStatus(bool ok);

enum class OtaState {
    Idle,
    Checking,
    Downloading,
    Verifying,
    Stored,
    ReadyForStm,
    Transferring,
    WaitStmConfirm,
    Done,
    Error
};

void otaMgrSetState(OtaState state);
OtaState otaMgrGetState();
const char* otaMgrStateText();