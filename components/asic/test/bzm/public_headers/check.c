#include BZM_PUBLIC_HEADER

#if __has_include("bzm/job.h") || __has_include("bzm/gpio.h") || __has_include("bzm/serial.h")
#error "ESP-Miner BZM adapter internals must not be exported to consumers"
#endif
