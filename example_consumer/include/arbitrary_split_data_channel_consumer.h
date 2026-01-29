
#ifndef ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_CONSUMER_H_
#define ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_CONSUMER_H_

#include <zephyr/kernel.h>

// TODO - zmk module, not zephyr driver

// device config structure
struct asdc_consumer_config {
    const struct device *asdc_channels[CONFIG_ZMK_ASDC_MAX_CHANNELS];
    size_t num_channels;
};

// device data structure
struct asdc_consumer_data {
    const struct device *dev;
    struct k_work_delayable hello_timer;
};

__subsystem struct asdc_consumer_driver_api {

};

#include <syscalls/arbitrary_split_data_channel.h>

#endif // ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_CONSUMER_H_