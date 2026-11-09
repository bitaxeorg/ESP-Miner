#ifndef OTA_GUARD_H_
#define OTA_GUARD_H_

#include "esp_partition.h"

typedef struct GlobalState GlobalState;

// App-level rollback for web firmware updates. The bootloader on shipped boards
// has no app rollback, so a new image that boots but crashes or can't mine would
// otherwise stay. After an update the new image is on probation: it is kept once
// it mines at a normal hashrate for 2 minutes, and the previous firmware boots
// again if the new one crashes at start-up twice, or is online but not mining
// 10 minutes after boot.

// At boot, right after NVS init and before the hardware comes up.
void ota_guard_boot(void);

// After an update was written to `updated` and set as the boot partition.
void ota_guard_arm(const esp_partition_t * updated);

// Starts the probation watcher if this boot is on probation.
void ota_guard_start(GlobalState * GLOBAL_STATE);

#endif /* OTA_GUARD_H_ */
