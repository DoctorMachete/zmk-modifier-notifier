if(CONFIG_ZMK_MODIFIER_NOTIFIER)
    zephyr_library()
    zephyr_library_sources(src/modifier_notifier.c)
endif()
