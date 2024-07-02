#ifndef SERIAL_H_
#define SERIAL_H_

#define CHUNK_SIZE 1024

int SERIAL_send(uint8_t *, int, bool);
void SERIAL_init(uint8_t pin_txd, uint8_t pin_rxd);
int16_t SERIAL_rx(uint8_t *, uint16_t, uint16_t);
void SERIAL_clear_buffer(void);
void SERIAL_set_baud(int baud);

#endif /* SERIAL_H_ */