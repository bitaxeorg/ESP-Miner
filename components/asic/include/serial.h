#ifndef SERIAL_H_
#define SERIAL_H_

#define SERIAL_BUF_SIZE 16
#define CHUNK_SIZE 1024

int SERIAL_send(uint8_t *, int, bool);
void SERIAL_init(void);
void SERIAL_debug_rx(void);
int16_t SERIAL_rx(uint8_t *, uint16_t, uint16_t);
void SERIAL_clear_buffer(void);
void SERIAL_set_baud(int baud);

esp_err_t SERIAL_init_BAP(void);
int SERIAL_send_BAP(uint8_t *, int, bool);
int16_t SERIAL_rx_BAP(uint8_t *, uint16_t, uint16_t);
void SERIAL_clear_buffer_BAP(void);
void SERIAL_set_baud_BAP(int baud);

#endif /* SERIAL_H_ */