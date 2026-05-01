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

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <net/wifi_prov_core/wifi_prov_core.h>
#include <bluetooth/services/wifi_provisioning.h>

#include <network_socket.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_manager);

#define NETWORK_THREAD_STACKSIZE		 			15200
#define NETWORK_THREAD_PRIORITY 	                6

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

#define ADV_DATA_UPDATE_INTERVAL      5
#define ADV_PARAM_UPDATE_DELAY        1

/* STEP 3.2 - Define indexes for accessing prov_svc_data */
#define ADV_DATA_VERSION_IDX          (BT_UUID_SIZE_128 + 0)
#define ADV_DATA_FLAG_IDX             (BT_UUID_SIZE_128 + 1)
#define ADV_DATA_FLAG_PROV_STATUS_BIT BIT(0)
#define ADV_DATA_FLAG_CONN_STATUS_BIT BIT(1)
#define ADV_DATA_RSSI_IDX             (BT_UUID_SIZE_128 + 3)

#define PROV_BT_LE_ADV_PARAM_FAST BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, \
						BT_GAP_ADV_FAST_INT_MIN_2, \
						BT_GAP_ADV_FAST_INT_MAX_2, NULL)

#define PROV_BT_LE_ADV_PARAM_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, \
						BT_GAP_ADV_SLOW_INT_MIN, \
						BT_GAP_ADV_SLOW_INT_MAX, NULL)

#define ADV_DAEMON_STACK_SIZE 4096
#define ADV_DAEMON_PRIORITY   5

#define MESSAGE_TO_SEND "Hello from nRF70 Series"
#define SSTRLEN(s) (sizeof(s) - 1)
#define RECV_BUF_SIZE 256 
static uint8_t recv_buf[RECV_BUF_SIZE];

//------------------------------------------------------------------------------

static struct net_mgmt_event_callback mgmt_cb;
static bool wifi_connected;
static K_SEM_DEFINE(run_app, 0, 1);

static uint8_t prov_svc_data[] = {BT_UUID_PROV_VAL, 0x00, 0x00, 0x00, 0x00};

static uint8_t device_name[] = {'P', 'V', '0', '0', '0', '0', '0', '0'};

static const struct bt_data ad[] = 
{
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_PROV_VAL),
	BT_DATA(BT_DATA_NAME_COMPLETE, device_name, sizeof(device_name)),
};

static const struct bt_data sd[] = 
{
	BT_DATA(BT_DATA_SVC_DATA128, prov_svc_data, sizeof(prov_svc_data)),
};

//------------------------------------------------------------------------------

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
			  uint64_t mgmt_event, struct net_if *iface)
{
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");
		wifi_connected = true;
		dk_set_led_on(DK_LED1);
		k_sem_give(&run_app);
		return;
	}
	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		if (wifi_connected == false) {
			LOG_INF("Waiting for network to be connected");
		} else {
			dk_set_led_off(DK_LED1);
			LOG_INF("Network disconnected");
			wifi_connected = false;
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

K_THREAD_STACK_DEFINE(adv_daemon_stack_area, ADV_DAEMON_STACK_SIZE);
static struct k_work_q adv_daemon_work_q;

/* STEP 6 - Define the work structures for updating advertisement parameters and data */
static struct k_work_delayable update_adv_param_work;
static struct k_work_delayable update_adv_data_work;

static void update_wifi_status_in_adv(void)
{
	/* STEP 5.1 - Update the firmware version*/
    prov_svc_data[ADV_DATA_VERSION_IDX] = PROV_SVC_VER;

    /* STEP 5.2 - Update the provisioning state */
    if (!wifi_prov_state_get())
        prov_svc_data[ADV_DATA_FLAG_IDX] &= ~ADV_DATA_FLAG_PROV_STATUS_BIT;
    else
        prov_svc_data[ADV_DATA_FLAG_IDX] |= ADV_DATA_FLAG_PROV_STATUS_BIT;

    /* STEP 5.3 - Update the Wi-Fi connection status*/
    struct net_if *iface = net_if_get_first_wifi();
    struct wifi_iface_status status = {0};

    int err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(struct wifi_iface_status));
    if ((err != 0) || (status.state < WIFI_STATE_ASSOCIATED))
    {
        prov_svc_data[ADV_DATA_FLAG_IDX] &= ~ADV_DATA_FLAG_CONN_STATUS_BIT;
        prov_svc_data[ADV_DATA_RSSI_IDX] = INT8_MIN;
    }
    else
    { /* WiFi is connected. */
        prov_svc_data[ADV_DATA_FLAG_IDX] |= ADV_DATA_FLAG_CONN_STATUS_BIT;
        prov_svc_data[ADV_DATA_RSSI_IDX] = status.rssi;
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("BT Connection failed (err 0x%02x).\n", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BT Connected: %s", addr);

	/* STEP 8.1 - Upon a connected event, cancel update_adv_data_work */
    k_work_cancel_delayable(&update_adv_data_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BT Disconnected: %s (reason 0x%02x).\n", addr, reason);

    /* STEP 8.2 - Upon a disconnected event, reschedule all work items*/
    k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_param_work, K_SECONDS(ADV_PARAM_UPDATE_DELAY));
    k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work, K_NO_WAIT);
}

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
				const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_INF("BT Identity resolved %s -> %s.\n", addr_rpa, addr_identity);
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("BT Security changed: %s level %u.\n", addr, level);
	} else {
		LOG_ERR("BT Security failed: %s level %u err %d.\n", addr, level, err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.identity_resolved = identity_resolved,
	.security_changed = security_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("BT Pairing cancelled: %s.\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BT pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_INF("BT Pairing Failed (%d). Disconnecting.\n", reason);
	bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
}

static struct bt_conn_auth_info_cb auth_info_cb_display = {

	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

static void update_adv_data_task(struct k_work *item)
{
    /* STEP 7.2 - Update the advertising and scan response data*/
    int err;

    update_wifi_status_in_adv();
    err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err != 0)
    {
        LOG_INF("Cannot update advertisement data, err = %d\n", err);
    }
    k_work_reschedule_for_queue(&adv_daemon_work_q, &update_adv_data_work,
                                K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
}

static void update_adv_param_task(struct k_work *item)
{
    /* STEP 7.1 - Stop advertising, then start advertising again */
    int err;

    err = bt_le_adv_stop();
    if (err != 0)
    {
        LOG_ERR("Cannot stop advertisement: err = %d\n", err);
        return;
    }

    err = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] & ADV_DATA_FLAG_PROV_STATUS_BIT
                              ? PROV_BT_LE_ADV_PARAM_SLOW
                              : PROV_BT_LE_ADV_PARAM_FAST,
                          ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err != 0)
    {
        LOG_ERR("Cannot start advertisement: err = %d\n", err);
    }
}

static void byte_to_hex(char *ptr, uint8_t byte, char base)
{
	int i, val;

	for (i = 0, val = (byte & 0xf0) >> 4; i < 2; i++, val = byte & 0x0f) {
		if (val < 10) {
			*ptr++ = (char) (val + '0');
		} else {
			*ptr++ = (char) (val - 10 + base);
		}
	}
}

static void update_dev_name(struct net_linkaddr *mac_addr)
{
	byte_to_hex(&device_name[2], mac_addr->addr[3], 'A');
	byte_to_hex(&device_name[4], mac_addr->addr[4], 'A');
	byte_to_hex(&device_name[6], mac_addr->addr[5], 'A');
}

//------------------------------------------------------------------------------


static void button_handler(uint32_t button_state, uint32_t has_changed)
{
    /* STEP 8 - Send a message every time button 1 is pressed */
    if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK)
    {
        int err = network_socket_send(MESSAGE_TO_SEND, SSTRLEN(MESSAGE_TO_SEND));
        if (err < 0)
        {
            LOG_INF("Failed to send message, %d", errno);
            return;
        }
    }
}

//------------------------------------------------------------------------------

static void network_thread_func(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

    if (dk_leds_init() != 0) 
    {
		LOG_ERR("Failed to initialize the LED library");
	}

    if (dk_buttons_init(button_handler) != 0) 
    {
		LOG_ERR("Failed to initialize the buttons library");
	}

    LOG_INF("Initializing Wi-Fi driver");
	/* Sleep to allow initialization of Wi-Fi driver */
	k_sleep(K_SECONDS(1));

	/* STEP 7 - Initialize and add the callback function for network events */
    net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&mgmt_cb);

    bt_conn_auth_cb_register(&auth_cb_display);
	bt_conn_auth_info_cb_register(&auth_info_cb_display);

	int err = bt_enable(NULL);
	if (err) 
    {
		LOG_ERR("Bluetooth init failed (err %d).\n", err);
		return;
	}
	LOG_INF("Bluetooth initialized.\n");

    /* STEP 9 - Enable the Bluetooth Wi-Fi Provisioning Service */
    err = wifi_prov_init();
    if (err == 0)
    {
        LOG_INF("Wi-Fi provisioning service starts successfully.\n");
    }
    else
    {
        LOG_ERR("Error occurs when initializing Wi-Fi provisioning service.\n");
        return;
    }

    /* STEP 10.1 Prepare the advertisement data */
    struct net_if *iface = net_if_get_default();
    struct net_linkaddr *mac_addr = net_if_get_link_addr(iface);
    char device_name_str[sizeof(device_name) + 1];

    if (mac_addr)
        update_dev_name(mac_addr);

    device_name_str[sizeof(device_name_str) - 1] = '\0';
    memcpy(device_name_str, device_name, sizeof(device_name));
    bt_set_name(device_name_str);

    /* STEP 10.2 - Start advertising */
    update_wifi_status_in_adv();

    err = bt_le_adv_start(prov_svc_data[ADV_DATA_FLAG_IDX] & ADV_DATA_FLAG_PROV_STATUS_BIT ? PROV_BT_LE_ADV_PARAM_SLOW : PROV_BT_LE_ADV_PARAM_FAST,
                          ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("BT Advertising failed to start (err %d)\n", err);
        return;
    }
    LOG_INF("BT Advertising successfully started.\n");

    k_work_queue_init(&adv_daemon_work_q);
	k_work_queue_start(&adv_daemon_work_q, adv_daemon_stack_area, K_THREAD_STACK_SIZEOF(adv_daemon_stack_area), ADV_DAEMON_PRIORITY, NULL);

	/* STEP 11 - Initializa all work items to their respective task */
    k_work_init_delayable(&update_adv_param_work, update_adv_param_task);
    k_work_init_delayable(&update_adv_data_work, update_adv_data_task);
    k_work_schedule_for_queue(&adv_daemon_work_q, &update_adv_data_work, K_SECONDS(ADV_DATA_UPDATE_INTERVAL));

	/* STEP 12 - Apply stored Wi-Fi credentials */
    net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

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

    k_msleep(5000);


    /* SOCKET test */

    int ret = network_socket_connect();
    if (ret < 0)
        LOG_ERR("Failed to connect to UDP server: %d", ret);


    while (1)
    {
        int received = network_socket_receive(recv_buf, sizeof(recv_buf) - 1);

        if (received < 0)
        {
            LOG_ERR("Socket error: %d, exit", errno);
            break;
        }

        if (received == 0)
        {
            LOG_ERR("Empty datagram");
            break;
        }

        recv_buf[received] = 0;
        LOG_INF("Data received from the server: (%s)", recv_buf);
        break;
    }

    network_socket_close();
    
    /* Performance test */



}

K_THREAD_DEFINE(network, NETWORK_THREAD_STACKSIZE, network_thread_func, NULL, NULL, NULL, NETWORK_THREAD_PRIORITY, 0, 0);

//------------------------------------------------------------------------------

