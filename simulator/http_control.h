/**
 * HTTP Control Server for Deskimon Simulator
 * Allows external control (e.g., from Swift Agent) via HTTP API
 * 
 * Endpoints:
 * - POST /emotion {"emotion": "happy"} - Set emotion by name
 * - POST /emotion/id {"id": 1} - Set emotion by ID
 * - POST /overlay {"kind": "status", "title": "Task Done", "subtitle": "Completed"}
 * - GET /status - Get current emotion and overlay status
 */

#pragma once

#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start HTTP control server on specified port (default: 8080) */
bool http_control_start(int port);

/* Stop HTTP control server */
void http_control_stop(void);

#ifdef __cplusplus
}
#endif
