/**
 * @file unity_config.h
 * @brief Unity test framework configuration for host-based testing
 */

#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

/* Use standard integer types */
#include <stdint.h>
#include <stddef.h>

/* Integer types configuration */
#define UNITY_INT_WIDTH 32
#define UNITY_LONG_WIDTH 64
#define UNITY_POINTER_WIDTH 64

/* Output configuration */
#define UNITY_OUTPUT_CHAR(c)    putchar(c)
#define UNITY_OUTPUT_FLUSH()    fflush(stdout)
#define UNITY_OUTPUT_START()
#define UNITY_OUTPUT_COMPLETE()

/* Enable floating point support */
#define UNITY_INCLUDE_FLOAT
#define UNITY_INCLUDE_DOUBLE

/* Enable 64-bit integer support */
#define UNITY_SUPPORT_64

/* Test fixture support */
#define UNITY_FIXTURE_NO_EXTRAS

/* Color output for terminal */
#define UNITY_OUTPUT_COLOR

#endif /* UNITY_CONFIG_H */
