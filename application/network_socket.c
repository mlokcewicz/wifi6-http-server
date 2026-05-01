//------------------------------------------------------------------------------

/// @file network_socket.c
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#include "network_socket.h"

#include <zephyr/net/socket.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

//------------------------------------------------------------------------------

LOG_MODULE_REGISTER(network_socket);

#define SERVER_HOSTNAME "udp-echo.nordicsemi.academy"
#define SERVER_PORT "2444"	

//------------------------------------------------------------------------------

static int sock;
static struct sockaddr_storage server;

//------------------------------------------------------------------------------

static int server_resolve(void)
{
    /* STEP 5.1 - Call getaddrinfo() to get the IP address of the echo server */
    int err;
    struct addrinfo *result;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM};

    err = getaddrinfo(SERVER_HOSTNAME, SERVER_PORT, &hints, &result);
    if (err != 0)
    {
        LOG_INF("getaddrinfo() failed, err: %d", err);
        return -EIO;
    }

    if (result == NULL)
    {
        LOG_INF("Error, address not found");
        return -ENOENT;
    }

    /* STEP 5.2 - Retrieve the relevant information from the result structure */
    struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);
    server4->sin_addr.s_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
    server4->sin_family = AF_INET;
    server4->sin_port = ((struct sockaddr_in *)result->ai_addr)->sin_port;

    /* STEP 5.3 - Convert the address into a string and print it */
    char ipv4_addr[NET_IPV4_ADDR_LEN];
    inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
    LOG_INF("IPv4 address of server found %s", ipv4_addr);

    /* STEP 5.4 - Free the memory allocated for result */
    freeaddrinfo(result);

    return 0;
}

static int server_connect(void)
{
    int err;
    /* STEP 6 - Create a UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOG_INF("Failed to create socket: %d.\n", errno);
        return -errno;
    }

    /* STEP 7 - Connect the socket to the server */

    err = connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr_in));
    if (err < 0)
    {
        LOG_INF("Connect failed : %d\n", errno);
        return -errno;
    }
    return 0;
}

//------------------------------------------------------------------------------

int network_socket_connect(void)
{
    if (server_resolve() != 0)
    {
        LOG_INF("Failed to resolve server name");
        return -1;
    }

    if (server_connect() != 0)
    {
        LOG_INF("Failed to connect to server");
        return -1;
    }

    return 0;
}

int network_socket_send(const char *msg, uint32_t len)
{
    return send(sock, msg, len, 0);
}

int network_socket_receive(char *buf, uint32_t len)
{
    return recv(sock, buf, len, 0);
}

void network_socket_close(void)
{
    zsock_close(sock);
}
