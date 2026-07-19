#ifndef WEBSOCKET_API_H_
#define WEBSOCKET_API_H_

void websocket_api_task(void *pvParameters);
void websocket_api_on_connect(int fd);
void websocket_api_send_share_found(double diff);

#endif /* WEBSOCKET_API_H_ */
