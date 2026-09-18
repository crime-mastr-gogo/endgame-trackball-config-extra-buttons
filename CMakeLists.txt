if(CONFIG_BOARD_EFOGTECH_TRBL_52833)
  zephyr_library()
  zephyr_library_include_directories(include)

  zephyr_library_sources(
    src/settings.c
    src/feedback.c
    src/controls.c
    src/pointer.c
    src/usb_routing.c
  )

  zephyr_link_libraries(-Wl,--wrap=zmk_esb_endpoint_is_active)
endif()
