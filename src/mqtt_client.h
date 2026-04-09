#pragma once
#include "config.h"

// Initialise the MQTT client. If mqtt_broker is empty the client is disabled.
// Must be called after WiFi is connected.
void mqtt_client_init(Config &cfg);

// Re-apply broker/port/topic changes without a reboot (call after saving config).
void mqtt_client_reinit();

// Must be called in loop() to maintain connection and process incoming messages.
void mqtt_client_tick();
