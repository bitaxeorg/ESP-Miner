#ifndef SERIAL_H_
#define SERIAL_H_

#define SERIAL_BUF_SIZE 16
#define CHUNK_SIZE 1024

int SERIAL_send(uint8_t *, int, bool);
esp_err_t SERIAL_init(void);
void SERIAL_debug_rx(void);
int16_t SERIAL_rx(uint8_t *, uint16_t, uint16_t);
void SERIAL_clear_buffer(void);
esp_err_t SERIAL_set_baud(int baud);

#endif /* SERIAL_H_ */