//------------------------------------------------------------------------------

/// @file network_manager.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#include "network_manager.h"

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_manager);

#define NETWORK_THREAD_STACKSIZE		 			15200
#define NETWORK_THREAD_PRIORITY 	                6

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

//------------------------------------------------------------------------------

static struct net_mgmt_event_callback mgmt_cb;
static bool connected;
static K_SEM_DEFINE(run_app, 0, 1);

//------------------------------------------------------------------------------

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
			  uint64_t mgmt_event, struct net_if *iface)
{
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");
		connected = true;
		dk_set_led_on(DK_LED1);
		k_sem_give(&run_app);
		return;
	}
	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		if (connected == false) {
			LOG_INF("Waiting for network to be connected");
		} else {
			dk_set_led_off(DK_LED1);
			LOG_INF("Network disconnected");
			connected = false;
		}
		k_sem_reset(&run_app);
		return;
	}
}

#ifdef CONFIG_WIFI_CREDENTIALS_STATIC 
/* STEP 8 - Define the function to populate the Wi-Fi credential parameters */
static int wifi_args_to_params(struct wifi_connect_req_params *params)
{

	/* STEP 8.1 - Populate the SSID and password */
    params->ssid = CONFIG_WIFI_CREDENTIALS_STATIC_SSID;
    params->ssid_length = strlen(params->ssid);

    params->psk = CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD;
    params->psk_length = strlen(params->psk);

	/* STEP 8.2 - Populate the rest of the relevant members */
    params->channel = WIFI_CHANNEL_ANY;
    params->security = WIFI_SECURITY_TYPE_PSK;
    params->mfp = WIFI_MFP_OPTIONAL;
    params->timeout = SYS_FOREVER_MS;
    params->band = WIFI_FREQ_BAND_UNKNOWN;
    memset(params->bssid, 0, sizeof(params->bssid));

	return 0;
}
#endif //CONFIG_WIFI_CREDENTIALS_STATIC

//------------------------------------------------------------------------------

static void network_thread_func(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

    if (dk_leds_init() != 0) {
		LOG_ERR("Failed to initialize the LED library");
	}

    LOG_INF("Initializing Wi-Fi driver");
	/* Sleep to allow initialization of Wi-Fi driver */
	k_sleep(K_SECONDS(1));

	/* STEP 7 - Initialize and add the callback function for network events */
    net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&mgmt_cb);

	#if CONFIG_WIFI_CREDENTIALS_STATIC

    struct net_if *iface = net_if_get_first_wifi();
    if (iface == NULL) {
        LOG_ERR("Returned network interface is NULL");
        return;
    }

    struct wifi_connect_req_params cnx_params = {0};

    wifi_args_to_params(&cnx_params);

    int err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(struct wifi_connect_req_params));
    if (err) {
        LOG_ERR("Connecting to Wi-Fi failed, err: %d", err);
        return;
    }

	#endif //CONFIG_WIFI_CREDENTIALS_STATIC

	k_sem_take(&run_app, K_FOREVER);

	while (1) 
	{
        
        k_msleep(100);
	}   
}

K_THREAD_DEFINE(network, NETWORK_THREAD_STACKSIZE, network_thread_func, NULL, NULL, NULL, NETWORK_THREAD_PRIORITY, 0, 0);

//------------------------------------------------------------------------------

