zephyr_library()

if (CONFIG_ZMK_ARBITRARY_SPLIT_DATA_CHANNEL)

  zephyr_syscall_header(include/arbitrary_split_data_channel.h)
  
  zephyr_library_sources(src/arbitrary_split_data_channel.c)

  if (CONFIG_ZMK_SPLIT_BLE)
    if (CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
      zephyr_library_sources(src/ble/arbitrary_split_data_channel_central.c)
    endif()
    if (CONFIG_ZMK_SPLIT AND (NOT CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
      zephyr_library_sources(src/ble/arbitrary_split_data_channel_peripheral.c)
    endif()
  endif()

  zephyr_include_directories(include)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

endif()