#ifndef WS_CLIENT_TASK_H_
#define WS_CLIENT_TASK_H_

#include <stdbool.h>

// FreeRTOS task entry point — the sole communication task.
// Connects to Hashly cloud via WebSocket, authenticates, and
// executes server-driven commands (poll, scan, restart, etc.).
void ws_client_task(void *pvParameters);

// Connection status accessors (safe to call from any task)
bool ws_client_is_connected(void);
bool ws_client_is_authenticated(void);
const char *ws_client_get_client_id(void);

#endif /* WS_CLIENT_TASK_H_ */
