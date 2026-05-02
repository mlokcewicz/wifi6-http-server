//------------------------------------------------------------------------------

/// @file network_http.h
/// @note Copyright (C) Michał Łokcewicz. All rights reserved.

//------------------------------------------------------------------------------

#ifndef NETWORK_HTTP_H_
#define NETWORK_HTTP_H_

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------

#include <stdbool.h>

//------------------------------------------------------------------------------

int network_http_client_connect(void);

int network_http_client_test(void);

//------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_HTTP_H_ */

//------------------------------------------------------------------------------
