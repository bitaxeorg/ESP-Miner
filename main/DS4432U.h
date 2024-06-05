#ifndef DS4432U_H_
#define DS4432U_H_

#include "i2c_master.h"

void DS4432U_read(void);
bool DS4432U_test(void);
bool DS4432U_set_vcore(float);

#endif /* DS4432U_H_ */
