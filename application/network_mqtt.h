//------------------------------------------------------------------------------

/// @file network_mqtt.h
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#ifndef NETWORK_MQTT_H_
#define NETWORK_MQTT_H_

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------

#include <stdbool.h>

//------------------------------------------------------------------------------

int network_mqtt_connect(void);

int network_mqtt_publish(int msg_no);

int network_mqtt_disconnect(void);

//------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_MQTT_H_ */

//------------------------------------------------------------------------------
