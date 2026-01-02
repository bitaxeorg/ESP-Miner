/**
 * @file test_main.c
 * @brief Main entry point for host-based unit tests
 *
 * This file runs all registered test suites.
 */

#include "unity.h"
#include <stdio.h>

/* Forward declarations of test suite runners */
extern void run_infrastructure_tests(void);
extern void run_power_management_calc_tests(void);
extern void run_autotune_state_tests(void);

/**
 * @brief Unity setUp function - called before each test
 */
void setUp(void)
{
    /* Reset any global mock state here */
}

/**
 * @brief Unity tearDown function - called after each test
 */
void tearDown(void)
{
    /* Clean up any test resources here */
}

/**
 * @brief Main test runner
 */
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf("================================================\n");
    printf("  ACS ESP-Miner Host-Based Unit Tests\n");
    printf("================================================\n");
    printf("\n");

    UNITY_BEGIN();

    /* Run all test suites */
    run_infrastructure_tests();
    run_power_management_calc_tests();
    run_autotune_state_tests();

    int result = UNITY_END();

    printf("\n");
    if (result == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("Some tests FAILED!\n");
    }
    printf("\n");

    return result;
}
