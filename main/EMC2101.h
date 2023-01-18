#ifndef EMC2101_H_
#define EMC2101_H_

void EMC2101_set_fan_speed(float);
void EMC2101_read(void);
uint32_t EMC2101_get_fan_speed(void);
void EMC2101_set_config(uint8_t);

#endif /* EMC2101_H_ */