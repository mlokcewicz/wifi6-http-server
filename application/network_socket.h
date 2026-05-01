//------------------------------------------------------------------------------

/// @file network_socket.h
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#ifndef NETWORK_SOCKET_H_
#define NETWORK_SOCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>

//------------------------------------------------------------------------------

int network_socket_connect(void);

int network_socket_send(const char *msg, uint32_t len);

int network_socket_receive(char *buf, uint32_t len);

void network_socket_close(void);

//------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_SOCKET_H_ */

//------------------------------------------------------------------------------
