#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>

void ADC_init(void);
void ADC_init_vcore(void);
uint16_t ADC_get_vcore(void);

#endif /* ADC_H_ */
