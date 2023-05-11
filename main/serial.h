#ifndef SERIAL_H_
#define SERIAL_H_

#define CHUNK_SIZE 1024

void SerialTask(void *arg);
int send_serial(uint8_t *, int);

#endif /* SERIAL_H_ */