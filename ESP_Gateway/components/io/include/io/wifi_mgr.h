// src/io/wifi_mgr.h
#pragma once
#include <string>

void wifiInit();
bool wifiIsConnected();
std::string wifiLocalIpStr();