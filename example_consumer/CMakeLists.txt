zephyr_library()

if (CONFIG_ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_CONSUMER)

  zephyr_syscall_header(include/arbitrary_split_data_channel_consumer.h)
  
  zephyr_library_sources(src/arbitrary_split_data_channel_consumer.c)

  zephyr_include_directories(include)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

endif()