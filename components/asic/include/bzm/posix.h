#ifndef BZM_POSIX_H
#define BZM_POSIX_H
#include "bzm/platform.h"
/* Fill timing/logging defaults, then supply fixture serial callbacks and bind.
 * Allocation defaults to libc. GPIO and rail control bind separately. */
void bzm_platform_posix_defaults(bzm_platform_ops_t *ops);
#endif
