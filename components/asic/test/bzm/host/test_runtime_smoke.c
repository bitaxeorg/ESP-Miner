#include "bzm/runtime.h"
#include "bzm/posix.h"

int main(void)
{
    bzm_platform_ops_t platform;
    bzm_platform_posix_defaults(&platform);
    if (!bzm_platform_bind(&platform, NULL)) return 1;
    if (!bzm_driver_clear_work(NULL)) return 2;
    return bzm_driver_read_temperature(NULL) == -1.0f ? 0 : 3;
}
