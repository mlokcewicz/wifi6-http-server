//------------------------------------------------------------------------------

/// @file network_perf.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#include "network_perf.h"

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include <zephyr/net/zperf.h>
#include <nrfx_clock.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_perf);

#define PEER_PORT 123
#define WIFI_ZPERF_PKT_SIZE 1024
#define WIFI_ZPERF_RATE 10000
#define WIFI_TEST_DURATION 20000

//------------------------------------------------------------------------------

static struct sockaddr_in in4_addr_my = {
	.sin_family = AF_INET,
	.sin_port = htons(PEER_PORT),
};

//------------------------------------------------------------------------------

static void udp_upload_results_cb(enum zperf_status status,
			  struct zperf_results *result,
			  void *user_data)
{
	unsigned int client_rate_in_kbps;

	/* Handle the three zperf session statuses: started, finished, and error */
	switch (status) {
	case ZPERF_SESSION_STARTED:
        /* STEP 7.1 - Inform the user that the UDP session has started */
        LOG_INF("New UDP session started");
        break;
    case ZPERF_SESSION_FINISHED:
        LOG_INF("Wi-Fi throughput test: Upload completed!");
        /* STEP 7.2 - If client_time_in_us is not zero, calculate the throughput rate in
         * kilobit per second. Otherwise, set it to zero */
        if (result->client_time_in_us != 0U)
        {
            client_rate_in_kbps = (uint32_t)(((uint64_t)result->nb_packets_sent *
                                              (uint64_t)result->packet_size * (uint64_t)8 *
                                              (uint64_t)USEC_PER_SEC) /
                                             ((uint64_t)result->client_time_in_us * 1024U));
        }
        else
        {
            client_rate_in_kbps = 0U;
        }

        /* STEP 7.3 - Print the results of the throughput test */
        LOG_INF("Upload results:");
        LOG_INF("%u bytes in %llu ms",
                (result->nb_packets_sent * result->packet_size),
                (result->client_time_in_us / USEC_PER_MSEC));
        LOG_INF("%u packets sent", result->nb_packets_sent);
        LOG_INF("%u packets lost", result->nb_packets_lost);
        LOG_INF("%u packets received", result->nb_packets_rcvd);
        LOG_INF("%u kbps throughput", client_rate_in_kbps);

        break;
	case ZPERF_SESSION_ERROR:
		/* STEP 7.4 - Inform the user that there is an error with the UDP session */
        LOG_ERR("UDP session error");

		break;
	case ZPERF_SESSION_PERIODIC_RESULT:
		break;
	}
}

//------------------------------------------------------------------------------

int network_perf_check(void)
{
    /* STEP 5.1 - Initialize a struct for storing the zperf upload parameters */
    struct zperf_upload_params params;

	/* STEP 5.2 - Configure packet size, rate and duration from the defines created earlier */
    params.packet_size = WIFI_ZPERF_PKT_SIZE;
    params.rate_kbps = WIFI_ZPERF_RATE;
    params.duration_ms = WIFI_TEST_DURATION;

	/* STEP 5.3 - Convert the server address from a string to IP address */
    int ret = net_addr_pton(AF_INET, CONFIG_NET_CONFIG_PEER_IPV4_ADDR, &in4_addr_my.sin_addr);
    if (ret < 0)
    {
        LOG_ERR("Invalid IPv4 address %s\n", CONFIG_NET_CONFIG_PEER_IPV4_ADDR);
        return -EINVAL;
    }

    LOG_INF("IPv4 address %s", CONFIG_NET_CONFIG_PEER_IPV4_ADDR);

	/* STEP 5.4 - Add the zperf server address to the zperf_upload_params struct */
    memcpy(&params.peer_addr, &in4_addr_my, sizeof(in4_addr_my));

	LOG_INF("Starting Wi-Fi throughput test: Zperf client");

	/* STEP 6 - Call zperf_udp_upload_async() to start the asynchronous UDP upload */
    ret = zperf_udp_upload_async(&params, udp_upload_results_cb, NULL);
    if (ret != 0)
    {
        LOG_ERR("Failed to start Wi-Fi benchmark: %d\n", ret);
        return ret;
    }

    return 0;
}