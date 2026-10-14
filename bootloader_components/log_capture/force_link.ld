/* Force the linker to include these symbols from the log_capture component */
EXTERN(rtc_write)
bootloader_console_init = capture_bootloader_console_init;
