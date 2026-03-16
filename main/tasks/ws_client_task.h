#ifndef WS_CLIENT_TASK_H_
#define WS_CLIENT_TASK_H_

// FreeRTOS task entry point — the sole communication task.
// Connects to Hashly cloud via WebSocket, authenticates, and
// executes server-driven commands (poll, scan, restart, etc.).
void ws_client_task(void *pvParameters);

#endif /* WS_CLIENT_TASK_H_ */
