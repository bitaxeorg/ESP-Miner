#include "bzm/esp.h"
#include <stdlib.h>
__attribute__((constructor)) static void initialize_bzm_platform(void)
{
    if (!bzm_platform_esp_init()) abort();
}
