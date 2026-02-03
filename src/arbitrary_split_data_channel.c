
#define DT_DRV_COMPAT zmk_arbitrary_split_data_channel

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <stdlib.h>

#include <arbitrary_split_data_channel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct asdc_event {
    const struct device *dev;
    size_t len;
    uint8_t *data;
};

int asdc_transport_init(const struct device *dev);
void asdc_transport_send_data(const struct device *dev, const uint8_t *data, size_t len);

K_MSGQ_DEFINE(asdc_tx_msgq, sizeof(struct asdc_event),
              CONFIG_ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_TX_QUEUE_SIZE, 1);
K_MSGQ_DEFINE(asdc_rx_msgq, sizeof(struct asdc_event),
              CONFIG_ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_RX_QUEUE_SIZE, 1);

void asdc_tx_work_callback(struct k_work *work) {
    struct asdc_event ev;
    while (k_msgq_get(&asdc_tx_msgq, &ev, K_NO_WAIT) == 0) {
        LOG_DBG("Sending asdc data: %u bytes", ev.len);
        asdc_transport_send_data(ev.dev, ev.data, ev.len);
        free(ev.data);
    }
}

void asdc_rx_work_callback(struct k_work *work) {
    struct asdc_event ev;
    while (k_msgq_get(&asdc_rx_msgq, &ev, K_NO_WAIT) == 0) {
        const struct device *dev = ev.dev;
        if (!dev) {
            LOG_ERR("No device for receiving data");
            continue;
        }

        struct asdc_data *asdc_data = (struct asdc_data *)dev->data;
        if (asdc_data->recv_cb == NULL) {
            LOG_WRN("No recv callback assigned on device %s", dev->name);
            continue;
        }
        asdc_data->recv_cb(dev, ev.data, ev.len);
        free(ev.data);
    }
}

static int asdc_init(const struct device *dev)
{
    return asdc_transport_init(dev);
}

K_WORK_DELAYABLE_DEFINE(asdc_tx_work, asdc_tx_work_callback);
K_WORK_DEFINE(asdc_rx_work, asdc_rx_work_callback);

const struct device* find_dev_for_channel_id(int channel_id) {
    const struct device *dev = NULL;
    #define ASDC_FIND_DEV(n)                                                                \
        do {                                                                                \
            const struct device *d = DEVICE_DT_INST_GET(n);                                 \
            if (d && device_is_ready(d)) {                                                  \
                const struct asdc_config *cfg = (const struct asdc_config *)d->config;      \
                if (cfg->channel_id == channel_id) {                                        \
                    dev = d;                                                                \
                    break;                                                                  \
                }                                                                           \
            }                                                                               \
        } while (0);
    DT_INST_FOREACH_STATUS_OKAY(ASDC_FIND_DEV)
    return dev;
}

static int asdc_send_data(const struct device *dev, const uint8_t *data, size_t len, uint32_t delay_ms)
{
    struct asdc_packet *packet = malloc(sizeof(struct asdc_packet) + len);
    if (!packet) {
        LOG_ERR("Failed to allocate memory for asdc_packet");
        return -ENOMEM;
    }

    memcpy(packet->data, data, len);
    packet->len = len;
    packet->channel_id = ((const struct asdc_config *)dev->config)->channel_id;

    struct asdc_event ev = {
        .dev = dev,
        .len = sizeof(struct asdc_packet) + len,
        .data = (uint8_t *)packet,
    };
    k_msgq_put(&asdc_tx_msgq, &ev, K_NO_WAIT);
    
    if (delay_ms > 0) {
        k_work_schedule(&asdc_tx_work, K_MSEC(delay_ms));
    } else {
        k_work_schedule(&asdc_tx_work, K_NO_WAIT);
    }

    return len;
}

void asdc_on_data_received(uint8_t *data, size_t len)
{
    LOG_DBG("asdc_on_data_received: received %zu bytes", len);
    
    if (len < sizeof(struct asdc_packet)) {
        LOG_ERR("Received data too small to contain asdc_packet header (need %zu, got %zu)", 
                sizeof(struct asdc_packet), len);
        return;
    }

    struct asdc_packet *packet = (struct asdc_packet *)data;
    
    LOG_DBG("asdc packet contains %u bytes of data on channel_id=%u", packet->len, packet->channel_id);

    if (packet->len + sizeof(packet->channel_id) + sizeof(packet->len) != len) {
        LOG_ERR("Received asdc data length mismatch, got %zu, expected %u",
                len, packet->len + sizeof(packet->channel_id) + sizeof(packet->len));
        return;
    }

    if (packet->len == 0) {
        LOG_ERR("Received asdc data with zero length");
        return;
    }

    // find the device for the channel_id
    const struct device *dev = find_dev_for_channel_id(packet->channel_id);
    if (!dev) {
        LOG_ERR("No device found for asdc channel ID %d", packet->channel_id);
        return;
    }

    uint8_t *data_copy = malloc(packet->len);
    if (!data_copy) {
        LOG_ERR("Failed to allocate memory for received asdc data");
        return;
    }
    memcpy(data_copy, packet->data, packet->len);

    struct asdc_event ev = {
        .dev = dev,
        .len = packet->len,
        .data = data_copy,
    };
    k_msgq_put(&asdc_rx_msgq, &ev, K_NO_WAIT);
    k_work_submit(&asdc_rx_work);
}

static void asdc_reg_recv_cb(const struct device *dev, asdc_rx_cb cb)
{
    struct asdc_data *asdc_data = (struct asdc_data *)dev->data;
    asdc_data->recv_cb = cb;
}

static const struct asdc_driver_api asdc_api = {
    .send = &asdc_send_data,
    .register_recv_cb = &asdc_reg_recv_cb,
};

//
// Define config structs for each instance
//

#define ASDC_CFG_DEFINE(n)                                                      \
    static const struct asdc_config config_##n = {                              \
        .channel_id = DT_INST_PROP(n, channel_id),                              \
    };

DT_INST_FOREACH_STATUS_OKAY(ASDC_CFG_DEFINE)

#define ASDC_DEVICE_DEFINE(n)                                                   \
    static struct asdc_data asdc_data_##n;                                      \
    DEVICE_DT_INST_DEFINE(n, asdc_init, NULL, &asdc_data_##n,                   \
                          &config_##n, POST_KERNEL,                             \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &asdc_api);

DT_INST_FOREACH_STATUS_OKAY(ASDC_DEVICE_DEFINE)
