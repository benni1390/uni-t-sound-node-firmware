#pragma once

#include "measurement.h"

// Configures the MQTT client when MQTT_HOST is set.
void mqtt_setup();
bool mqtt_enabled();
// Reconnects if needed and services the client.
void mqtt_loop();
void mqtt_publish_connection_state();
// Publishes the reading, changed state and periodic diagnostics.
void mqtt_publish_measurement(const Measurement &current);
