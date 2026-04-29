STM32 Master, ESP32 Kommunikationsmodul

UART:
115200 8N1
ASCII line based
Line ending: \n

STM -> ESP:
PING
STATUS?

ESP -> STM:
PONG
STATUS:WIFI_OK:TIME_OK:OTA_IDLE
ERROR:<code>

Timeouts:
PONG timeout: 12s
STATUS stale timeout: 30s
PING interval: 5s
STATUS interval: 15s