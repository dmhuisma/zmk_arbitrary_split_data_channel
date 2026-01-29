
#ifndef ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_H_
#define ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_H_

#ifdef CONFIG_ZMK_SPLIT_BLE
#include <zephyr/bluetooth/uuid.h>
#define ZMK_BT_ASDC_UUID(num)           BT_UUID_128_ENCODE(num, 0x0096, 0x7107, 0xc967, 0xc5cfb1c2482c)
#define ZMK_BT_ASDC_SERVICE_UUID        ZMK_BT_ASDC_UUID(0x0001234a)
#define ZMK_BT_ASDC_CHAR_UUID           ZMK_BT_ASDC_UUID(0x0001234b)
#endif

// device config structure
struct asdc_config {
    int channel_id;
};

typedef int (*asdc_rx_cb)(const struct device *dev, uint8_t *buf, size_t buflen);

typedef int (*asdc_tx)(const struct device *dev, const uint8_t *data, size_t len);
typedef void (*asdc_register_rx_cb)(const struct device *dev, asdc_rx_cb cb);

// device runtime data structure
struct asdc_data {
    asdc_rx_cb recv_cb;
    struct bt_conn *conn;
};

struct asdc_packet {
    int channel_id;
    size_t len;
    uint8_t data[];
};

__subsystem struct asdc_driver_api {
    asdc_tx send;
    asdc_register_rx_cb register_recv_cb;
};

__syscall int asdc_send(const struct device *dev, const uint8_t *data, size_t len);

static inline int z_impl_asdc_send(const struct device *dev, const uint8_t *data, size_t len)
{
    const struct asdc_driver_api *api = (const struct asdc_driver_api *)dev->api;
	if (api->send == NULL) {
		return -ENOSYS;
	}
	return api->send(dev, data, len);
}

__syscall void asdc_register_recv_cb(const struct device *dev, asdc_rx_cb cb);

static inline void z_impl_asdc_register_recv_cb(const struct device *dev, asdc_rx_cb cb)
{
    const struct asdc_driver_api *api = (const struct asdc_driver_api *)dev->api;
	if (api->register_recv_cb == NULL) {
		return;
	}
	api->register_recv_cb(dev, cb);
}

void asdc_on_data_received(uint8_t *data, size_t len);

#ifdef CONFIG_ZMK_SPLIT_BLE
void asdc_store_connection(struct bt_conn *conn);
void asdc_clear_connection(struct bt_conn *conn);
#endif

#include <syscalls/arbitrary_split_data_channel.h>

#endif // ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_H_
