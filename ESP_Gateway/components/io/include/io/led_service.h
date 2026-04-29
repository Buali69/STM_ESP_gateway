#pragma once
#include <stdint.h>

enum class LedMode : uint8_t {
  Normal,
  Ota,
  NewFw,
  Error
};

void ledInit(int pin);
void ledSetMode(LedMode mode);
void ledTick();   // regelmäßig aus ioTask aufrufen