#include <stdio.h>
#include <string.h>
#include "unity.h"

static void print_banner(const char *text);

void app_main(void)
{
    print_banner("Running all the registered tests");
    UNITY_BEGIN();
    unity_run_tests_by_tag("[not-on-qemu]", true);
    int failures = UNITY_END();

#if defined(__XTENSA__)
    register int a2 __asm__("a2") = 1;  /* TARGET_SYS_exit */
    register int a3 __asm__("a3") = (failures > 0) ? 1 : 0;
    __asm__ volatile("simcall" : : "r"(a2), "r"(a3));
#endif

    exit(failures > 0 ? 1 : 0);
}

static void print_banner(const char *text)
{
    printf("\n#### %s #####\n\n", text);
}