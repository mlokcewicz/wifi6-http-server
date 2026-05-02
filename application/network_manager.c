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
#include <zephyr/net/conn_mgr_monitor.h>

#if CONFIG_BT_WIFI_PROV
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <net/wifi_prov_core/wifi_prov_core.h>
#include <bluetooth/services/wifi_provisioning.h>
#endif

#include <network_socket.h>
#include <network_perf.h>
#include <network_mqtt.h>
#include <network_http.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_manager);

#define NETWORK_THREAD_STACKSIZE 8200
#define NETWORK_THREAD_PRIORITY 6

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

#define TWT_WAKE_INTERVAL_MS 65
#define TWT_INTERVAL_MS 7000

#define TWT_MGMT_EVENTS (NET_EVENT_WIFI_TWT | NET_EVENT_WIFI_TWT_SLEEP_STATE)

#define ADV_DATA_UPDATE_INTERVAL 5
#define ADV_PARAM_UPDATE_DELAY 1

/* STEP 3.2 - Define indexes for accessing prov_svc_data */
#define ADV_DATA_VERSION_IDX (BT_UUID_SIZE_128 + 0)
#define ADV_DATA_FLAG_IDX (BT_UUID_SIZE_128 + 1)
#define ADV_DATA_FLAG_PROV_STATUS_BIT BIT(0)
#define ADV_DATA_FLAG_CONN_STATUS_BIT BIT(1)
#define ADV_DATA_RSSI_IDX (BT_UUID_SIZE_128 + 3)

#define PROV_BT_LE_ADV_PARAM_FAST BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,        \
                                                  BT_GAP_ADV_FAST_INT_MIN_2, \
                                                  BT_GAP_ADV_FAST_INT_MAX_2, NULL)

#define PROV_BT_LE_ADV_PARAM_SLOW BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,      \
                                                  BT_GAP_ADV_SLOW_INT_MIN, \
                                                  BT_GAP_ADV_SLOW_INT_MAX, NULL)

#define ADV_DAEMON_STACK_SIZE 4096
#define ADV_DAEMON_PRIORITY 5

#define TCP_MESSAGE_TO_SEND "Hello from nRF70 Series"
#define SSTRLEN(s) (sizeof(s) - 1)
#define RECV_BUF_SIZE 256
static uint8_t recv_buf[RECV_BUF_SIZE];

//------------------------------------------------------------------------------

static uint32_t twt_flow_id = 1;

static struct net_mgmt_event_callback mgmt_cb;
static struct net_mgmt_event_callback twt_mgmt_cb;
static bool wifi_connected;
static K_SEM_DEFINE(run_app, 0, 1);

#if CONFIG_BT_WIFI_PROV
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
#endif

//------------------------------------------------------------------------------

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event, struct net_if *iface)
{
    if ((mgmt_event & EVENT_MASK) != mgmt_event)
    {
        return;
    }
    if (mgmt_event == NET_EVENT_L4_CONNECTED)
    {
        LOG_INF("Network connected");
        wifi_connected = true;
        dk_set_led_on(DK_LED1);
        k_sem_give(&run_app);
        return;
    }
    if (mgmt_event == NET_EVENT_L4_DISCONNECTED)
    {
        if (wifi_connected == false)
        {
            LOG_INF("Waiting for network to be connected");
        }
        else
        {
            dk_set_led_off(DK_LED1);
#ifdef CONFIG_MQTT_HELPER
            network_mqtt_disconnect();
#endif
            LOG_INF("Network disconnected");
            wifi_connected = false;
        }
        k_sem_reset(&run_app);
        return;
    }
}

#ifdef CONFIG_WIFI_CREDENTIALS_STATIC
/* Define the function to populate the Wi-Fi credential parameters */
static int wifi_args_to_params(struct wifi_connect_req_params *params)
{

    /*Populate the SSID and password */
    params->ssid = CONFIG_WIFI_CREDENTIALS_STATIC_SSID;
    params->ssid_length = strlen(params->ssid);

    params->psk = CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD;
    params->psk_length = strlen(params->psk);

    /* Populate the rest of the relevant members */
    params->channel = WIFI_CHANNEL_ANY;
    params->security = WIFI_SECURITY_TYPE_PSK;
    params->mfp = WIFI_MFP_OPTIONAL;
    params->timeout = SYS_FOREVER_MS;
    params->band = WIFI_FREQ_BAND_UNKNOWN;
    memset(params->bssid, 0, sizeof(params->bssid));

    return 0;
}
#endif // CONFIG_WIFI_CREDENTIALS_STATIC

//------------------------------------------------------------------------------
#if CONFIG_BT_WIFI_PROV

K_THREAD_STACK_DEFINE(adv_daemon_stack_area, ADV_DAEMON_STACK_SIZE);
static struct k_work_q adv_daemon_work_q;

/* STEP 6 - Define the work structures for updating advertisement parameters and data */
static struct k_work_delayable update_adv_param_work;
static struct k_work_delayable update_adv_data_work;

static void update_wifi_status_in_adv(void)
{
    /* Update the firmware version*/
    prov_svc_data[ADV_DATA_VERSION_IDX] = PROV_SVC_VER;

    /* Update the provisioning state */
    if (!wifi_prov_state_get())
        prov_svc_data[ADV_DATA_FLAG_IDX] &= ~ADV_DATA_FLAG_PROV_STATUS_BIT;
    else
        prov_svc_data[ADV_DATA_FLAG_IDX] |= ADV_DATA_FLAG_PROV_STATUS_BIT;

    /* Update the Wi-Fi connection status*/
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

    if (err)
    {
        LOG_ERR("BT Connection failed (err 0x%02x).\n", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("BT Connected: %s", addr);

    /* Upon a connected event, cancel update_adv_data_work */
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

    if (!err)
    {
        LOG_INF("BT Security changed: %s level %u.\n", addr, level);
    }
    else
    {
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

    for (i = 0, val = (byte & 0xf0) >> 4; i < 2; i++, val = byte & 0x0f)
    {
        if (val < 10)
        {
            *ptr++ = (char)(val + '0');
        }
        else
        {
            *ptr++ = (char)(val - 10 + base);
        }
    }
}

static void update_dev_name(struct net_linkaddr *mac_addr)
{
    byte_to_hex(&device_name[2], mac_addr->addr[3], 'A');
    byte_to_hex(&device_name[4], mac_addr->addr[4], 'A');
    byte_to_hex(&device_name[6], mac_addr->addr[5], 'A');
}
#endif
//------------------------------------------------------------------------------

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
    /* STEP 8 - Send a message every time button 1 is pressed */
    if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK)
    {
        network_socket_connect();

        int err = network_socket_send(TCP_MESSAGE_TO_SEND, SSTRLEN(TCP_MESSAGE_TO_SEND));
        if (err < 0)
        {
            LOG_INF("Failed to send message, %d", errno);
        }

        network_socket_close();
#ifdef CONFIG_MQTT_HELPER
        err = network_mqtt_publish(0);
        if (err < 0)
        {
            LOG_INF("Failed to publish on MQTT topic message, %d", err);
        }
#endif
    }
    if (has_changed & DK_BTN2_MSK && button_state & DK_BTN2_MSK)
    {
#ifdef CONFIG_MQTT_HELPER
        int err = network_mqtt_publish(1);
        if (err < 0)
        {
            LOG_INF("Failed to publish on MQTT topic message, %d", err);
        }
#endif
        network_http_client_test(false);
    }
}

//------------------------------------------------------------------------------

int wifi_set_power_state(bool enable)
{
    struct net_if *iface = net_if_get_first_wifi();

    /* Define the Wi-Fi power save parameters structure */
    struct wifi_ps_params ps_params = {0};

    /* Check if power saving is currently enabled */
    ps_params.enabled = enable ? WIFI_PS_ENABLED : WIFI_PS_DISABLED;

    /* Send the power save request */
    if (net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps_params, sizeof(ps_params)))
    {
        LOG_ERR("Power save %s failed. Reason %s", ps_params.enabled ? "enable" : "disable",
                wifi_ps_get_config_err_code_str(ps_params.fail_reason));
        return -1;
    }
    LOG_INF("Set power save: %s", ps_params.enabled ? "enable" : "disable");

    return 0;
}

int wifi_set_ps_wakeup_mode(bool listen_interval)
{
    struct net_if *iface = net_if_get_default();

    /* Define the Wi-Fi power save parameters structure */
    struct wifi_ps_params ps_params = {0};

    /* Check and toggle the current wakeup mode */
    ps_params.wakeup_mode = listen_interval ? WIFI_PS_WAKEUP_MODE_LISTEN_INTERVAL : WIFI_PS_WAKEUP_MODE_DTIM;

    /* Set the request type to wakeup mode. */
    ps_params.type = WIFI_PS_PARAM_WAKEUP_MODE;

    /* Send the wakeup mode request */
    if (net_mgmt(NET_REQUEST_WIFI_PS, iface, &ps_params, sizeof(ps_params)))
    {
        LOG_ERR("Setting wakeup mode failed. Reason %s",
                wifi_ps_get_config_err_code_str(ps_params.fail_reason));
        return -1;
    }
    LOG_INF("Set wakeup mode: %s", ps_params.wakeup_mode ? "listen interval" : "DTIM");

    return 0;
}

static void handle_wifi_twt_event(struct net_mgmt_event_callback *cb)
{
    /* Create a struct for the received TWT event */
    const struct wifi_twt_params *resp = (const struct wifi_twt_params *)cb->info;

    /* Upon a TWT teardown initiated by the AP, toggle the state */
    if (resp->operation == WIFI_TWT_TEARDOWN)
    {
        LOG_INF("TWT teardown received for flow ID %d\n",
                resp->flow_id);
        return;
    }

    /* Update the flow ID received in the TWT response */
    twt_flow_id = resp->flow_id;

    /* Check if a TWT response was received */
    if (resp->resp_status == WIFI_TWT_RESP_RECEIVED)
    {
        LOG_INF("TWT response: %s",
                wifi_twt_setup_cmd_txt(resp->setup_cmd));
    }
    else
    {
        LOG_INF("TWT response timed out\n");
        return;
    }

    /* Upon an accepted TWT setup, log the negotiated parameters */
    if (resp->setup_cmd == WIFI_TWT_SETUP_CMD_ACCEPT)
    {

        LOG_INF("== TWT negotiated parameters ==");
        LOG_INF("TWT Dialog token: %d",
                resp->dialog_token);
        LOG_INF("TWT flow ID: %d",
                resp->flow_id);
        LOG_INF("TWT negotiation type: %s",
                wifi_twt_negotiation_type_txt(resp->negotiation_type));
        LOG_INF("TWT responder: %s",
                resp->setup.responder ? "true" : "false");
        LOG_INF("TWT implicit: %s",
                resp->setup.implicit ? "true" : "false");
        LOG_INF("TWT announce: %s",
                resp->setup.announce ? "true" : "false");
        LOG_INF("TWT trigger: %s",
                resp->setup.trigger ? "true" : "false");
        LOG_INF("TWT wake interval: %d ms (%d us)",
                resp->setup.twt_wake_interval / USEC_PER_MSEC,
                resp->setup.twt_wake_interval);
        LOG_INF("TWT interval: %lld s (%lld us)",
                resp->setup.twt_interval / USEC_PER_SEC,
                resp->setup.twt_interval);
        LOG_INF("===============================");
    }
}

static void twt_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
                                   struct net_if *iface)
{
    switch (mgmt_event)
    {
    /* STEP 3.2.1 - Upon a TWT event, call handle_wifi_twt_event() to handle the response */
    case NET_EVENT_WIFI_TWT:
        handle_wifi_twt_event(cb);
        break;

    /* STEP 3.2.2 -	Upon TWT sleep state event, inform the user of the current sleep state */
    case NET_EVENT_WIFI_TWT_SLEEP_STATE:
        int *twt_state;
        twt_state = (int *)(cb->info);
        LOG_INF("TWT sleep state: %s", *twt_state ? "awake" : "sleeping");
        if ((*twt_state == WIFI_TWT_STATE_AWAKE))
        {
            network_socket_connect();

            int err = network_socket_send(TCP_MESSAGE_TO_SEND, SSTRLEN(TCP_MESSAGE_TO_SEND));
            if (err < 0)
            {
                LOG_INF("Failed to send message, %d", errno);
            }

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

            network_socket_close();
        }
        break;
    }
}

int wifi_set_twt(bool enable)
{
    struct net_if *iface = net_if_get_first_wifi();

    /* Define the TWT parameters struct */
    struct wifi_twt_params twt_params = {0};

    twt_params.negotiation_type = WIFI_TWT_INDIVIDUAL;
    twt_params.flow_id = twt_flow_id;
    twt_params.dialog_token = 1;

    if (enable)
    {
        /* Fill in the TWT setup specific parameters */
        twt_params.operation = WIFI_TWT_SETUP;
        twt_params.setup_cmd = WIFI_TWT_SETUP_CMD_REQUEST;
        twt_params.setup.responder = 0;
        twt_params.setup.trigger = 0;
        twt_params.setup.implicit = 1;
        twt_params.setup.announce = 0;
        twt_params.setup.twt_wake_interval = TWT_WAKE_INTERVAL_MS * USEC_PER_MSEC;
        twt_params.setup.twt_interval = TWT_INTERVAL_MS * USEC_PER_MSEC;
    }
    else
    {
        /* Fill in the TWT teardown specific parameters */
        twt_params.operation = WIFI_TWT_TEARDOWN;
        twt_params.setup_cmd = WIFI_TWT_TEARDOWN;
        twt_flow_id = twt_flow_id < WIFI_MAX_TWT_FLOWS ? twt_flow_id + 1 : 1;
    }

    /* Send the TWT request with net_mgmt */
    if (net_mgmt(NET_REQUEST_WIFI_TWT, iface, &twt_params, sizeof(twt_params)))
    {
        LOG_ERR("%s with %s failed, reason : %s",
                wifi_twt_operation_txt(twt_params.operation),
                wifi_twt_negotiation_type_txt(twt_params.negotiation_type),
                wifi_twt_get_err_code_str(twt_params.fail_reason));
        return -1;
    }

    LOG_INF("-------------------------------");
    LOG_INF("TWT operation %s requested", wifi_twt_operation_txt(twt_params.operation));
    LOG_INF("-------------------------------");
    return 0;
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

    net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_event_handler, EVENT_MASK);
    net_mgmt_add_event_callback(&mgmt_cb);

    net_mgmt_init_event_callback(&twt_mgmt_cb, twt_mgmt_event_handler, TWT_MGMT_EVENTS);
    net_mgmt_add_event_callback(&twt_mgmt_cb);

#if CONFIG_BT_WIFI_PROV
    bt_conn_auth_cb_register(&auth_cb_display);
    bt_conn_auth_info_cb_register(&auth_info_cb_display);

    int err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d).\n", err);
        return;
    }
    LOG_INF("Bluetooth initialized.\n");

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
#endif

    struct net_if *iface = net_if_get_default();
#if CONFIG_BT_WIFI_PROV
    struct net_linkaddr *mac_addr = net_if_get_link_addr(iface);
    char device_name_str[sizeof(device_name) + 1];

    if (mac_addr)
        update_dev_name(mac_addr);

    device_name_str[sizeof(device_name_str) - 1] = '\0';
    memcpy(device_name_str, device_name, sizeof(device_name));
    bt_set_name(device_name_str);

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

    k_work_init_delayable(&update_adv_param_work, update_adv_param_task);
    k_work_init_delayable(&update_adv_data_work, update_adv_data_task);
    k_work_schedule_for_queue(&adv_daemon_work_q, &update_adv_data_work, K_SECONDS(ADV_DATA_UPDATE_INTERVAL));
#endif

    net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

#if CONFIG_WIFI_CREDENTIALS_STATIC

    struct net_if *iface = net_if_get_first_wifi();
    if (iface == NULL)
    {
        LOG_ERR("Returned network interface is NULL");
        return;
    }

    struct wifi_connect_req_params cnx_params = {0};

    wifi_args_to_params(&cnx_params);

    int err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(struct wifi_connect_req_params));
    if (err)
    {
        LOG_ERR("Connecting to Wi-Fi failed, err: %d", err);
        return;
    }

#endif // CONFIG_WIFI_CREDENTIALS_STATIC

    LOG_INF("Waiting for Wi-Fi connection");

    conn_mgr_mon_resend_status();

    k_sem_take(&run_app, K_FOREVER);

    wifi_set_power_state(true);
    wifi_set_ps_wakeup_mode(true);
    wifi_set_twt(true);

    int ret = 0;

    /* SOCKET test */

    (void)recv_buf;
#ifdef SOCKET_TEST

    ret = network_socket_connect();
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
#endif

    /* Performance test */
#ifdef CONFIG_NET_ZPERF
    network_perf_check();
#endif

    /* MQTT test */
#ifdef CONFIG_MQTT_HELPER
    network_mqtt_connect();
#endif

    network_http_client_connect(false);
    network_http_client_test(false);

    network_http_server_start();

    while (1)
    {
        k_msleep(1000);
    }

    (void)ret;
}

K_THREAD_DEFINE(network, NETWORK_THREAD_STACKSIZE, network_thread_func, NULL, NULL, NULL, NETWORK_THREAD_PRIORITY, 0, 0);

//------------------------------------------------------------------------------
