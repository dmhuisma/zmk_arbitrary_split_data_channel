
#define DT_DRV_COMPAT zmk_arbitrary_split_data_channel_consumer

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <arbitrary_split_data_channel.h>
#include <arbitrary_split_data_channel_consumer.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void asdc_rx_callback(const struct device *dev, uint8_t *data, size_t len) {
    // this is just a sample, print the data received
    LOG_INF("ASDC data received on device %s: len=%d", dev->name, len);
    // print data as string
    char buf[256];
    size_t print_len = len < sizeof(buf) ? len : sizeof(buf);
    memcpy(buf, data, print_len);
    LOG_INF("Data: %s", buf);
}

static void send_asdc_message(const struct device *asdc_dev, uint8_t* data, size_t length)
{
    int ret = asdc_send(asdc_dev, data, length);
    if (ret < 0) {
        LOG_ERR("Failed to send ASDC message on device %s: %d", asdc_dev->name, ret);
    } else {
        LOG_INF("Sent ASDC message on device %s: len=%d", asdc_dev->name, length);
    }
}

static void hello_timer_handler(struct k_work *work)
{
    struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);
    struct asdc_consumer_data *data = CONTAINER_OF(delayable_work, struct asdc_consumer_data, hello_timer);
    const struct asdc_consumer_config *config = (const struct asdc_consumer_config *)data->dev->config;

    // Send "hello" to all ASDC channels
    uint8_t message[] = "Hello! This is an example of a large message more than the BLE MTU size.";
    for (size_t i = 0; i < config->num_channels; i++) {
        const struct device *asdc_dev = config->asdc_channels[i];
        send_asdc_message(asdc_dev, message, sizeof(message));
    }

    // Reschedule for next second
    k_work_schedule(&data->hello_timer, K_SECONDS(5));
}

static int asdcc_init(const struct device *dev)
{
    const struct asdc_consumer_config *config = (const struct asdc_consumer_config *)dev->config;
    struct asdc_consumer_data *data = (struct asdc_consumer_data *)dev->data;
    
    for (size_t i = 0; i < config->num_channels; i++) {
        const struct device *asdc_dev = config->asdc_channels[i];
        if (!device_is_ready(asdc_dev)) {
            LOG_ERR("ASDC channel device %s not ready", asdc_dev->name);
            return -ENODEV;
        }
    }

    // get the asdc devices and register callbacks
    for (size_t i = 0; i < config->num_channels; i++) {
        const struct device *asdc_dev = config->asdc_channels[i];
        asdc_register_recv_cb(asdc_dev, (asdc_rx_cb)asdc_rx_callback);
    }

    // Store device reference and initialize timer
    data->dev = dev;
    k_work_init_delayable(&data->hello_timer, hello_timer_handler);
    
    // Start the timer to send "hello" every second
    k_work_schedule(&data->hello_timer, K_SECONDS(5));

    return 0;
}

//
// Define config structs for each instance
//

#define ASDCC_CHANNEL_GET(idx, n)                                               \
    DEVICE_DT_GET(DT_INST_PHANDLE_BY_IDX(n, asdc_channels, idx))

#define ASDCC_CFG_DEFINE(n)                                                     \
    static const struct asdc_consumer_config config_##n = {                     \
        .num_channels = DT_INST_PROP_LEN(n, asdc_channels),                     \
        .asdc_channels = {                                                      \
            LISTIFY(DT_INST_PROP_LEN(n, asdc_channels),                         \
                    ASDCC_CHANNEL_GET, (,), n)                                  \
        },                                                                      \
    };

DT_INST_FOREACH_STATUS_OKAY(ASDCC_CFG_DEFINE)

#define ASDCC_DEVICE_DEFINE(n)                                                  \
    static struct asdc_consumer_data asdcc_data_##n;                            \
    DEVICE_DT_INST_DEFINE(n, asdcc_init, NULL, &asdcc_data_##n,                 \
                          &config_##n, POST_KERNEL,                             \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);

DT_INST_FOREACH_STATUS_OKAY(ASDCC_DEVICE_DEFINE)