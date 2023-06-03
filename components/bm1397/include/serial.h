#ifndef SERIAL_H_
#define SERIAL_H_

#define CHUNK_SIZE 1024

void SerialTask(void *arg);
int send_serial(uint8_t *, int, bool);
void init_serial(void);
void debug_serial_rx(void);
int16_t serial_rx(uint8_t *, uint16_t, uint16_t);
void clear_serial_buffer(void);
void set_max_baud(void);

#endif /* SERIAL_H_ */