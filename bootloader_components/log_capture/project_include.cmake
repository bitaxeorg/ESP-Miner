if(BOOTLOADER_BUILD)
    # Force the linker to include our symbols using a linker script EXTERN directive.
    # This property is evaluated by the bootloader subproject.
    idf_build_set_property(BOOTLOADER_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/force_link.ld" APPEND)
endif()
