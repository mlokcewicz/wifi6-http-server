//------------------------------------------------------------------------------

/// @file network_mqtt.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#include "network_mqtt.h"

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/net/socket.h>

#include <net/mqtt_helper.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_mqtt);

#define MESSAGE_BUFFER_SIZE 128

#define LED1_ON_CMD       "LED1ON"
#define LED1_OFF_CMD      "LED1OFF"
#define LED2_ON_CMD       "LED2ON"
#define LED2_OFF_CMD      "LED2OFF"
#define BUTTON1_MSG       "Button 1 pressed"
#define BUTTON2_MSG       "Button 2 pressed"

#define SUBSCRIBE_TOPIC_ID 1234

//------------------------------------------------------------------------------

static uint8_t client_id[sizeof(CONFIG_BOARD) + 11];

//------------------------------------------------------------------------------

/* STEP 6 - Define the function to subscribe to topics */
static void subscribe(void)
{
	int err;

    /* STEP 6.1 - Declare a variable of type mqtt_topic */
    struct mqtt_topic subscribe_topic = 
    {
        .topic = {
            .utf8 = CONFIG_MQTT_SAMPLE_SUB_TOPIC,
            .size = strlen(CONFIG_MQTT_SAMPLE_SUB_TOPIC)},
        .qos = MQTT_QOS_1_AT_LEAST_ONCE
    };

    /* STEP 6.2 - Define a subscription list */
    struct mqtt_subscription_list subscription_list = 
    {
        .list = &subscribe_topic,
        .list_count = 1,
        .message_id = SUBSCRIBE_TOPIC_ID
    };

    /* STEP 6.3 - Subscribe to topics */
    LOG_INF("Subscribing to %s", CONFIG_MQTT_SAMPLE_SUB_TOPIC);
	err = mqtt_helper_subscribe(&subscription_list);
	if (err) 
    {
		LOG_ERR("Failed to subscribe to topics, error: %d", err);
		return;
	}

}

/* STEP 7 - Define the function to publish data */
static int publish(uint8_t *data, size_t len)
{
	int err;
	/* STEP 7.1 - Declare and populate a variable of type mqtt_publish_param */	
    struct mqtt_publish_param mqtt_param;

	mqtt_param.message.payload.data = data;
	mqtt_param.message.payload.len = len;
	mqtt_param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	mqtt_param.message_id = mqtt_helper_msg_id_get(),
	mqtt_param.message.topic.topic.utf8 = CONFIG_MQTT_SAMPLE_PUB_TOPIC;
	mqtt_param.message.topic.topic.size = strlen(CONFIG_MQTT_SAMPLE_PUB_TOPIC);
	mqtt_param.dup_flag = 0;
	mqtt_param.retain_flag = 0;


	/* STEP 7.2 - Publish to MQTT broker */
    err = mqtt_helper_publish(&mqtt_param);
	if (err) 
    {
		LOG_WRN("Failed to send payload, err: %d", err);
		return err;
	}

	LOG_INF("Published message: \"%.*s\" on topic: \"%.*s\"", mqtt_param.message.payload.len,
								  mqtt_param.message.payload.data,
								  mqtt_param.message.topic.topic.size,
								  mqtt_param.message.topic.topic.utf8);

	return 0;
}

/* STEP 8.1 - Define callback handler for CONNACK event */
static void on_mqtt_connack(enum mqtt_conn_return_code return_code, bool session_present)
{
	if (return_code == MQTT_CONNECTION_ACCEPTED) {
		LOG_INF("Connected to MQTT broker");
		LOG_INF("Hostname: %s", CONFIG_MQTT_SAMPLE_BROKER_HOSTNAME);
		LOG_INF("Client ID: %s", (char *)client_id);
		LOG_INF("Port: %d", CONFIG_MQTT_HELPER_PORT);
		LOG_INF("TLS: %s", IS_ENABLED(CONFIG_MQTT_LIB_TLS) ? "Yes" : "No");
		subscribe();
	} else {
		LOG_WRN("Connection to broker not established, return_code: %d", return_code);
	}
}

/* STEP 8.2 - Define callback handler for SUBACK event */
static void on_mqtt_suback(uint16_t message_id, int result)
{	
	if (result != MQTT_SUBACK_FAILURE) {
		if (message_id == SUBSCRIBE_TOPIC_ID) {
			LOG_INF("Subscribed to %s with QoS %d", CONFIG_MQTT_SAMPLE_SUB_TOPIC, result);
			return;
		}
		LOG_WRN("Subscribed to unknown topic, id: %d with QoS %d", message_id, result);
		return;
	}
	LOG_ERR("Topic subscription failed, error: %d", result);
}

/* STEP 8.3 - Define callback handler for PUBLISH event */
static void on_mqtt_publish(struct mqtt_helper_buf topic, struct mqtt_helper_buf payload)
{
	LOG_INF("Received payload: %.*s on topic: %.*s", payload.size,
							 payload.ptr,
							 topic.size,
							 topic.ptr);

    if (strncmp(payload.ptr, LED1_ON_CMD, sizeof(LED1_ON_CMD) - 1) == 0)
        dk_set_led_on(DK_LED1);
    else if (strncmp(payload.ptr, LED1_OFF_CMD, sizeof(LED1_OFF_CMD) - 1) == 0)
        dk_set_led_off(DK_LED1);
    else if (strncmp(payload.ptr, LED2_ON_CMD, sizeof(LED2_ON_CMD) - 1) == 0)
        dk_set_led_on(DK_LED2);
    else if (strncmp(payload.ptr, LED2_OFF_CMD, sizeof(LED2_OFF_CMD) - 1) == 0)
        dk_set_led_off(DK_LED2);
}

/* STEP 8.4 - Define callback handler for DISCONNECT event */
static void on_mqtt_disconnect(int result)
{
	LOG_INF("MQTT client disconnected: %d", result);
}

int network_mqtt_connect(void)
{
    /* STEP 9 - Initialize the MQTT helper library */
    struct mqtt_helper_cfg config = {
        .cb = {
            .on_connack = on_mqtt_connack,
            .on_disconnect = on_mqtt_disconnect,
            .on_publish = on_mqtt_publish,
            .on_suback = on_mqtt_suback,
        },
    };

    int err = mqtt_helper_init(&config);
	if (err) {
		LOG_ERR("Failed to initialize MQTT helper, error: %d", err);
		return err;
	}

    /* STEP 10.2 - Generate the client ID */
    uint32_t id = sys_rand32_get();
    snprintf(client_id, sizeof(client_id), "%s-%010u", CONFIG_BOARD, id);


    /* STEP 11 - Establish a connection the MQTT broker */
    struct mqtt_helper_conn_params conn_params = {
        .hostname.ptr = CONFIG_MQTT_SAMPLE_BROKER_HOSTNAME,
        .hostname.size = strlen(CONFIG_MQTT_SAMPLE_BROKER_HOSTNAME),
        .device_id.ptr = (char *)client_id,
        .device_id.size = strlen(client_id),
    };

    err = mqtt_helper_connect(&conn_params);
    if (err)
    {
        LOG_ERR("Failed to connect to MQTT, error code: %d", err);
        return err;
    }

    return 0;
}

int network_mqtt_publish(int msg_no)
{
    int err = 0;

    if (msg_no == 0)
        err = publish(BUTTON1_MSG, sizeof(BUTTON1_MSG) - 1);
    else if (msg_no == 1)
        err = publish(BUTTON2_MSG, sizeof(BUTTON2_MSG) - 1);

    if (err)
    {
        LOG_ERR("Failed to send message, %d", err);
        return err;
    }

    return 0;
}

int network_mqtt_disconnect(void)
{
    return mqtt_helper_disconnect();
}

//------------------------------------------------------------------------------
