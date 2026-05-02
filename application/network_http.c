//------------------------------------------------------------------------------

/// @file network_http.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#include "network_http.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

#include <zephyr/app_version.h>
#include <zephyr/version.h>
#include <ncs_version.h>
#include <ncs_commit.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/logging/log.h>

#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/parser.h>

#include <pages/pages.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_http);

#define CONFIG_HTTP_SAMPLE_HOSTNAME "rest.nordicsemi.academy"
#define CONFIG_HTTP_SAMPLE_PORT "80"
#define CONFIG_HTTP_SAMPLE_PORT_TLS "443"
#define CONFIG_HTTP_SERVER_SAMPLE_PORT 80

#define HTTP_TLS_SEC_TAG 42

#define RECV_BUF_SIZE 2048
#define CLIENT_ID_SIZE 36

#define MAX_CLIENT_QUEUE		2
#define STACK_SIZE			    4096
#define THREAD_PRIORITY			K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES - 1)

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

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

static char page_buf[4096];

/* Register service to be advertised via DNS */
DNS_SD_REGISTER_TCP_SERVICE(http_server_sd, CONFIG_NET_HOSTNAME, "_http", "local", DNS_SD_EMPTY_TXT, CONFIG_HTTP_SERVER_SAMPLE_PORT);

/* Define a struct to represent the structure of HTTP requests */
struct http_req 
{
	struct http_parser parser;
	int socket;
	bool received_all;
	enum http_method method;
	const char *url;
	size_t url_len;
	const char *body;
	size_t body_len;
};

/* Forward declarations */
static void process_tcp4(void);

/* Keep track of the current LED states. 0 = LED OFF, 1 = LED ON.
 * Index 0 corresponds to LED1, index 1 to LED2.
 */
static uint8_t led_states[2];

/* Define HTTP server response strings */
static const char response_200[] = "HTTP/1.1 200 OK\r\n";
static const char response_403[] = "HTTP/1.1 403 Forbidden\r\n\r\n";
static const char response_404[] = "HTTP/1.1 404 Not Found\r\n\r\n";

/* Set up threads for handling multiple incoming TCP connections simultaneously */
K_THREAD_STACK_ARRAY_DEFINE(tcp4_handler_stack, MAX_CLIENT_QUEUE, STACK_SIZE);
static struct k_thread tcp4_handler_thread[MAX_CLIENT_QUEUE];
static k_tid_t tcp4_handler_tid[MAX_CLIENT_QUEUE];
K_THREAD_DEFINE(tcp4_thread_id, STACK_SIZE, process_tcp4, NULL, NULL, NULL, THREAD_PRIORITY, 0, -1);

/* Declare and initialize structs and variables used in network management */
static int tcp4_listen_sock;
static int tcp4_accepted[MAX_CLIENT_QUEUE];

/* Define a variable to store the HTTP parser settings */
static struct http_parser_settings parser_settings;

/* Define a function to update the LED state */
static bool handle_led_update(struct http_req *request, size_t led_id)
{
	char new_state_tmp[2] = {0};
	uint8_t new_state;
	size_t led_index = led_id - 1;

	if (request->body_len < 1) {
		return false;
	}

	memcpy(new_state_tmp, request->body, 1);

	new_state = atoi(request->body);

	if (new_state <= 1) {
		led_states[led_index] = new_state;
	} else {
		LOG_WRN("Attempted to update LED%d state to illegal value %d",
			led_id, new_state);
		return false;
	}

	(void)dk_set_led(led_index, led_states[led_index]);
	LOG_INF("LED%d turned %s", led_id, new_state == 1 ? "on" : "off");

	return true;
}

/* Define a function to handle HTTP requests */
static void handle_http_request(struct http_req *request)
{
	size_t len;
	const char *resp_ptr = response_403;
	char dynamic_response_buf[100];
	char url[100];
	size_t url_len = MIN(sizeof(url) - 1, request->url_len);

	memcpy(url, request->url, url_len);

	url[url_len] = '\0';

	if (request->method == HTTP_PUT) {
		size_t led_id;
		int ret = sscanf(url, "/led/%u", &led_id);

		LOG_INF("Received PUT request to %s", url);

		/* Handle PUT requests to the "led" resource */
		if ((ret == 1) && (led_id > 0) && (led_id < (ARRAY_SIZE(led_states) + 1))) {
			if (handle_led_update(request, led_id)) {
				(void)snprintk(dynamic_response_buf, sizeof(dynamic_response_buf),
						"%s\r\n", response_200);
				resp_ptr = dynamic_response_buf;
			}
		} else {
			LOG_INF("Attempt to update unsupported resource '%s'", url);
			resp_ptr = response_403;
		}
	}

	if (request->method == HTTP_GET) {
		size_t led_id;
		int ret = sscanf(url, "/led/%u", &led_id);

		LOG_INF("Received GET request to %s", url);

		/* Handle GET requests to the "led" resource */
		if ((ret == 1) && (led_id > 0) && (led_id < (ARRAY_SIZE(led_states) + 1))) {
			char body[2];
			size_t led_index = led_id - 1;

			(void)snprintk(body, sizeof(body), "%d", led_states[led_index]);

			(void)snprintk(dynamic_response_buf,      sizeof(dynamic_response_buf),
				       "%sContent-Length: %d\r\n\r\n%s",
				       response_200, strlen(body), body);

			resp_ptr = dynamic_response_buf;
		} 
		else if (strcmp(url, "/") == 0)
		{
			static char version_str[128];

			snprintf(version_str, sizeof(version_str),
             "App: %s-%s\n"
             "NCS: %s-%s\n"
             "Zephyr: %s-%s\n",
             APP_VERSION_STRING, STR(APP_BUILD_VERSION),
             NCS_VERSION_STRING, NCS_COMMIT_STRING,
             KERNEL_VERSION_STRING, STR(BUILD_VERSION));

	    	uint32_t uptime = k_uptime_get() / 1000;

        	snprintf(page_buf, sizeof(page_buf), index_page, uptime, version_str);

			resp_ptr = page_buf;
		} 
		else 
		{
			LOG_INF("Attempt to fetch unknown resource '%.*s'", request->url_len, request->url);

			resp_ptr = response_404;
		}
	}

	/* Get the total length of the HTTP response */
	len = strlen(resp_ptr);

	while (len) {
		ssize_t out_len;

		out_len = send(request->socket, resp_ptr, len, 0);

		if (out_len < 0) 
		{
			LOG_ERR("Error while sending: %d", -errno);
			return;
		}

		resp_ptr = (const char *)resp_ptr + out_len;
		len -= out_len;
	}
}

/* Define callbacks for the HTTP parser */
static int on_body(struct http_parser *parser, const char *at, size_t length)
{
	struct http_req *req = CONTAINER_OF(parser, struct http_req, parser);

	req->body = at;
	req->body_len = length;

	LOG_DBG("on_body: %d", parser->method);
	LOG_DBG("> %.*s", length, at);

	return 0;
}

static int on_headers_complete(struct http_parser *parser)
{
	struct http_req *req = CONTAINER_OF(parser, struct http_req, parser);

	req->method = parser->method;

	LOG_DBG("on_headers_complete, method: %s", http_method_str(parser->method));

	return 0;
}

static int on_message_begin(struct http_parser *parser)
{
	struct http_req *req = CONTAINER_OF(parser, struct http_req, parser);

	req->received_all = false;

	LOG_DBG("on_message_begin, method: %d", parser->method);

	return 0;
}

static int on_message_complete(struct http_parser *parser)
{
	struct http_req *req = CONTAINER_OF(parser, struct http_req, parser);

	req->received_all = true;

	LOG_DBG("on_message_complete, method: %d", parser->method);

	return 0;
}

static int on_url(struct http_parser *parser, const char *at, size_t length)
{
	struct http_req *req = CONTAINER_OF(parser, struct http_req, parser);

	req->url = at;
	req->url_len = length;

	LOG_DBG("on_url, method: %d", parser->method);
	LOG_DBG("> %.*s", length, at);

	return 0;
}

/* Define function to initialize the callbacks */
static void parser_init(void)
{
	http_parser_settings_init(&parser_settings);

	parser_settings.on_body = on_body;
	parser_settings.on_headers_complete = on_headers_complete;
	parser_settings.on_message_begin = on_message_begin;
	parser_settings.on_message_complete = on_message_complete;
	parser_settings.on_url = on_url;
}

/* Setup the HTTP server */
static int setup_server(int *sock, struct sockaddr *bind_addr, socklen_t bind_addrlen)
{
	int ret;

	*sock = socket(bind_addr->sa_family, SOCK_STREAM, IPPROTO_TCP);

	if (*sock < 0) {
		LOG_ERR("Failed to create TCP socket: %d", errno);
		return -errno;
	}

	ret = bind(*sock, bind_addr, bind_addrlen);
	if (ret < 0) {
		LOG_ERR("Failed to bind TCP socket %d", errno);
		return -errno;
	}

	ret = listen(*sock, MAX_CLIENT_QUEUE);
	if (ret < 0) {
		LOG_ERR("Failed to listen on TCP socket %d", errno);
		ret = -errno;
	}

	return ret;
}

/* Setup the TCP client connection handler */
static void client_conn_handler(void *ptr1, void *ptr2, void *ptr3)
{
	ARG_UNUSED(ptr1);
	int *sock = ptr2;
	k_tid_t *in_use = ptr3;
	int received;
	int ret;
	char buf[1024];
	size_t offset = 0;
	size_t total_received = 0;
	struct http_req request = 
    {
		.socket = *sock,
	};

	http_parser_init(&request.parser, HTTP_REQUEST);

	while (1) {
  	/* Receive TCP fragment */
		received = recv(request.socket, buf + offset, sizeof(buf) - offset, 0);
		if (received == 0) {
			/* Connection closed */
			LOG_INF("[%d] Connection closed by peer", request.socket);
			break;
		} else if (received < 0) {
			/* Socket error */
			ret = -errno;
			LOG_ERR("[%d] Connection error %d", request.socket, ret);
			break;
		}

		/* Parse the received data as HTTP request */
		(void)http_parser_execute(&request.parser,
					  &parser_settings, buf + offset, received);

		total_received += received;
		offset += received;

		if (offset >= sizeof(buf)) {
			offset = 0;
		}

   /* If HTTP request completely received, process the request */
		if (request.received_all) {
			handle_http_request(&request);
			break;
		}
	};

	(void)close(request.socket);

	*sock = -1;
	*in_use = NULL;
}

static int get_free_slot(int *accepted)
{
	int i;

	for (i = 0; i < MAX_CLIENT_QUEUE; i++) {
		if (accepted[i] < 0) {
			return i;
		}
	}

	return -1;
}

/* Define a function to handle incoming TCP connections */

static int process_tcp(int *sock, int *accepted)
{
	static int counter;
	int client;
	int slot;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	char addr_str[INET_ADDRSTRLEN];

	client = accept(*sock, (struct sockaddr *)&client_addr,
			&client_addr_len);
	if (client < 0) {
		LOG_ERR("Error in accept %d, stopping server", -errno);
		return -errno;
	}

	slot = get_free_slot(accepted);
	if (slot < 0 || slot >= MAX_CLIENT_QUEUE) {
		LOG_ERR("Cannot accept more connections");
		close(client);
		return 0;
	}

	accepted[slot] = client;

    if (client_addr.sin_family == AF_INET)
    {
        tcp4_handler_tid[slot] = k_thread_create(
            &tcp4_handler_thread[slot], tcp4_handler_stack[slot],
            K_THREAD_STACK_SIZEOF(tcp4_handler_stack[slot]),
            (k_thread_entry_t)client_conn_handler, INT_TO_POINTER(slot),
            &accepted[slot], &tcp4_handler_tid[slot], THREAD_PRIORITY, 0, K_NO_WAIT);
    }

    net_addr_ntop(client_addr.sin_family, &client_addr.sin_addr, addr_str, sizeof(addr_str));

	LOG_DBG("[%d] Connection #%d from %s", client, ++counter, addr_str);

	return 0;
}

/* Define a function to process incoming IPv4 clients */
static void process_tcp4(void)
{
	int ret;
	struct sockaddr_in addr4 = {
		.sin_family = AF_INET,
		.sin_port =  htons(CONFIG_HTTP_SERVER_SAMPLE_PORT),
	};

	ret = setup_server(&tcp4_listen_sock, (struct sockaddr *)&addr4, sizeof(addr4));
	if (ret < 0) {
		return;
	}

	LOG_INF("Waiting for IPv4 HTTP connections on port %d, sock %d", CONFIG_HTTP_SERVER_SAMPLE_PORT, tcp4_listen_sock);
    LOG_INF("Connect to %s.%s:%d", CONFIG_NET_HOSTNAME, "local", CONFIG_HTTP_SERVER_SAMPLE_PORT);

	while (ret == 0) {
		ret = process_tcp(&tcp4_listen_sock, tcp4_accepted);
	}
}

/* Define function to start listening on TCP socket */
void start_listener(void)
{
	for (size_t i = 0; i < MAX_CLIENT_QUEUE; i++) {
		tcp4_accepted[i] = -1;
		tcp4_listen_sock = -1;

	}
	k_thread_start(tcp4_thread_id);
}

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

int network_http_server_start(void)
{
    parser_init();
    start_listener();

    return 0;
}