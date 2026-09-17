if(CONFIG_BOARD_EFOGTECH_TRBL_52833)
  # The pinned EC11-ish module compiles its source unconditionally even when
  # CONFIG_EC11_ISH=n, while its log-level Kconfig symbol then disappears.
  # Keep the driver disabled and supply only the missing compile-time constant.
  zephyr_compile_definitions(CONFIG_EC11_ISH_LOG_LEVEL=0)
  zephyr_library()
  zephyr_library_include_directories(include)
  zephyr_library_sources(src/settings.c src/feedback.c src/controls.c src/pointer.c src/usb_routing.c)
  zephyr_link_libraries(-Wl,--wrap=zmk_esb_endpoint_is_active)
endif()
