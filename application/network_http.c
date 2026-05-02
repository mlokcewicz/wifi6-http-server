//------------------------------------------------------------------------------

/// @file network_http.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#include "network_http.h"

#include <zephyr/logging/log.h>

#include <zephyr/net/socket.h>

#include <zephyr/net/http/client.h>
#include <zephyr/net/tls_credentials.h>

#include <stdio.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_http);

#define CONFIG_HTTP_SAMPLE_HOSTNAME "rest.nordicsemi.academy"
#define CONFIG_HTTP_SAMPLE_PORT "80"
#define CONFIG_HTTP_SAMPLE_PORT_TLS "443"

#define HTTP_TLS_SEC_TAG 42

#define RECV_BUF_SIZE 2048
#define CLIENT_ID_SIZE 36

//------------------------------------------------------------------------------

static int sock;
static struct sockaddr_storage server;

static char recv_buf[RECV_BUF_SIZE];
static char client_id_buf[CLIENT_ID_SIZE + 2];

static int counter = 0;

static const char ca_certificate[] = 
{
	#include "AmazonRootCA1.pem.inc"
	IF_ENABLED(CONFIG_TLS_CREDENTIALS, (0x00))
};

//------------------------------------------------------------------------------

static int server_resolve(bool use_tls)
{
    int err;
    struct zsock_addrinfo *result;
    struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

    err = zsock_getaddrinfo(CONFIG_HTTP_SAMPLE_HOSTNAME, use_tls ? CONFIG_HTTP_SAMPLE_PORT_TLS : CONFIG_HTTP_SAMPLE_PORT, &hints, &result);
    if (err != 0)
    {
        LOG_ERR("getaddrinfo failed, err: %d, %s", err, zsock_gai_strerror(err));
        return -EIO;
    }

    if (result == NULL)
    {
        LOG_ERR("Error, address not found");
        return -ENOENT;
    }

    struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);
    server4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
    server4->sin_family = AF_INET;
    server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

    char ipv4_addr[NET_IPV4_ADDR_LEN];
    zsock_inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
    LOG_INF("IPv4 address of HTTP server found %s", ipv4_addr);

    zsock_freeaddrinfo(result);

    return 0;
}

static int server_connect(bool use_tls)
{
	int err;
	sock = zsock_socket(AF_INET, SOCK_STREAM, use_tls ? IPPROTO_TLS_1_2 : IPPROTO_TCP);
    if (sock < 0)
    {
        LOG_ERR("Failed to set up HTTPS socket, err: %d, %s", errno, strerror(errno));
        return -errno;
    }

    if (use_tls)
    {
        sec_tag_t sec_tag_opt[] =
        {
            HTTP_TLS_SEC_TAG,
        };

        err = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_opt, sizeof(sec_tag_opt));
        if (err)
        {
            LOG_ERR("Failed to set TLS security TAG list, err: %d", errno);
            (void)close(sock);
            return -errno;
        }

        err = setsockopt(sock, SOL_TLS, TLS_HOSTNAME, CONFIG_HTTP_SAMPLE_HOSTNAME, sizeof(CONFIG_HTTP_SAMPLE_HOSTNAME));
        if (err)
        {
            LOG_ERR("Failed to set TLS_HOSTNAME option, err: %d", errno);
            (void)close(sock);
            return -errno;
        }
    }

    err = zsock_connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connecting to server failed, err: %d, %s", errno, strerror(errno));
		return -errno;
	}

	LOG_INF("Connected to server");
	return 0;
}

static int response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    /* STEP 9 - Define the callback function to print the body */
    LOG_INF("Response status: %s", rsp->http_status);

    if (rsp->body_frag_len > 0)
    {
        char body_buf[rsp->body_frag_len];
        strncpy(body_buf, rsp->body_frag_start, rsp->body_frag_len);
        body_buf[rsp->body_frag_len] = '\0';
        LOG_INF("Received: %s", body_buf);
    }

    LOG_INF("Closing socket: %d", sock);
    close(sock);
    return 0;
}

static int client_id_cb(struct http_response *rsp, enum http_final_call final_data,
                        void *user_data)
{
    /* STEP 6.1 - Log the HTTP response status */
    LOG_INF("Response status: %s", rsp->http_status);

    /* STEP 6.2 - Retrieve and format the client ID */
    char client_id_buf_tmp[CLIENT_ID_SIZE + 1];
    strncpy(client_id_buf_tmp, rsp->body_frag_start, CLIENT_ID_SIZE);
    client_id_buf_tmp[CLIENT_ID_SIZE] = '\0';
    client_id_buf[0] = '/';
    strcat(client_id_buf, client_id_buf_tmp);

    LOG_INF("Successfully acquired client ID: %s", client_id_buf);
    /* STEP 6.3 - Close the socket */
    LOG_INF("Closing socket: %d", sock);
    close(sock);

    return 0;
}

static int client_http_put(void)
{
    /* STEP 7 - Define the function to send a PUT request to the HTTP server */
    int err = 0;
    int bytes_written;
    const char *headers[] = {"Connection: close\r\n", NULL};

    struct http_request req;
    memset(&req, 0, sizeof(req));

    char buffer[12] = {0};
    bytes_written = snprintf(buffer, 12, "%d", counter);
    if (bytes_written < 0)
    {
        LOG_ERR("Unable to write to buffer, err: %d", bytes_written);
        return bytes_written;
    }
    req.header_fields = headers;
    req.method = HTTP_PUT;
    req.url = client_id_buf;
    req.host = CONFIG_HTTP_SAMPLE_HOSTNAME;
    req.protocol = "HTTP/1.1";
    req.payload = buffer;
    req.payload_len = bytes_written;
    req.response = response_cb;
    req.recv_buf = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    LOG_INF("HTTP PUT request: %s", buffer);
    err = http_client_req(sock, &req, 5000, NULL);
    if (err < 0)
    {
        LOG_ERR("Failed to send HTTP PUT request %s, err: %d", buffer, err);
    }

    return err;
}

static int client_http_get(void)
{
    /* STEP 8 - Define the function to send a GET request to the HTTP server */

    int err = 0;
    const char *headers[] = {"Connection: close\r\n", NULL};

    struct http_request req;
    memset(&req, 0, sizeof(req));

    req.header_fields = headers;
    req.method = HTTP_GET;
    req.url = client_id_buf;
    req.host = CONFIG_HTTP_SAMPLE_HOSTNAME;
    req.protocol = "HTTP/1.1";
    req.response = response_cb;
    req.recv_buf = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    LOG_INF("HTTP GET request");
    err = http_client_req(sock, &req, 5000, NULL);
    if (err < 0)
    {
        LOG_ERR("Failed to send HTTP GET request, err: %d", err);
    }

    return err;
}

static int client_get_new_id(void)
{
    int err = 0;

    /* STEP 5.1 - Define the structure http_request and fill the block of memory */
    struct http_request req;
    memset(&req, 0, sizeof(req));

    /* STEP 5.2 - Populate the http_request structure */
    const char *headers[] = {"Connection: close\r\n", NULL};
    req.header_fields = headers;
    req.method = HTTP_POST;
    req.url = "/new";
    req.host = CONFIG_HTTP_SAMPLE_HOSTNAME;
    req.protocol = "HTTP/1.1";
    req.response = client_id_cb;
    req.recv_buf = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    /* STEP 5.3 - Send the request to the HTTP server */
    LOG_INF("HTTP POST request");
    err = http_client_req(sock, &req, 5000, NULL);
    if (err < 0)
    {
        LOG_ERR("Failed to send HTTP POST request, err: %d", err);
    }

    return err;
}

static int setup_credentials(void)
{
	LOG_INF("Provisioning server certificate");

    int err = tls_credential_add(HTTP_TLS_SEC_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca_certificate, sizeof(ca_certificate));
	if (err == -EEXIST){
		LOG_ERR("Certificate already exists, sec tag: %d", HTTP_TLS_SEC_TAG);
	} else if (err < 0) {
		LOG_ERR("Failed to provision server certificate: %d", err);
	}

	return err;
}

//------------------------------------------------------------------------------

int network_http_client_connect(bool use_tls)
{
    if (server_resolve(use_tls) != 0)
    {
        LOG_ERR("Failed to resolve server name");
        return -1;
    }

    if (use_tls)
    {

        if (setup_credentials() != 0)
        {
            LOG_ERR("Setup credentials failed");
        }
    }

    LOG_INF("Connecting to %s:%s", CONFIG_HTTP_SAMPLE_HOSTNAME, CONFIG_HTTP_SAMPLE_PORT_TLS);
    if (server_connect(use_tls) != 0)
    {
        LOG_ERR("Failed to connect to server");
        return -1;
    }

    /* STEP 11 - Retrieve the client ID upon connection */
    if (client_get_new_id() < 0)
    {
        LOG_INF("Failed to get client ID");
        return -1;

        /* STEP 12 - Send a PUT request to HTTP server */
        if (server_connect(use_tls) >= 0)
        {
            client_http_put();
            counter++;
        }
    }

    return 0;
}

int network_http_client_test(bool use_tls)
{
    if (server_connect(use_tls) >= 0)
    {
        client_http_put();
        counter++;
    }

    k_msleep(500);

    if (server_connect(use_tls) >= 0)
    {
        client_http_get();
    }

    return 0;
}