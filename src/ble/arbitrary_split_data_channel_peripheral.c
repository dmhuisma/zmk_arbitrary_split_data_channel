
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

#include <arbitrary_split_data_channel.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

//
// ZMK Split Peripheral Connection Status Event Handler
//

static void find_first_conn(struct bt_conn *conn, void *data) {
    struct bt_conn **cp = (struct bt_conn **)data;
    *cp = conn;
}

static int asdc_split_peripheral_status_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev = 
        as_zmk_split_peripheral_status_changed(eh);
    
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Get the active connection from ZMK's peripheral
    struct bt_conn *conn = NULL;
    if (ev->connected) {
        bt_conn_foreach(BT_CONN_TYPE_LE, find_first_conn, &conn);
    }

    if (ev->connected) {
        char addr[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        LOG_DBG("ASDC device connected: %s", addr);
        asdc_store_connection(conn);
    } else {
        LOG_DBG("ASDC device disconnected");
        asdc_clear_connection(NULL);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(asdc_split_peripheral, asdc_split_peripheral_status_listener);
ZMK_SUBSCRIPTION(asdc_split_peripheral, zmk_split_peripheral_status_changed);

//
// Transport-specific functions
//

int asdc_transport_init(const struct device *dev) {
    return 0;
}

void asdc_transport_send_data(const struct device *dev, const uint8_t *data, size_t len) {
    struct asdc_data *asdc_data = (struct asdc_data *)dev->data;
    const struct bt_uuid* uuid = BT_UUID_DECLARE_128(ZMK_BT_ASDC_CHAR_UUID);
    struct bt_gatt_attr *attr = bt_gatt_find_by_uuid(NULL, 0, uuid);
    if (!attr) {
        LOG_ERR("ASDC attribute not found for UUID");
        return;
    }
    struct bt_conn* conn = asdc_data->conn;
    if (!conn) {
        LOG_ERR("No active connection for ASDC data send");
        return;
    }
	int err = bt_gatt_notify(conn, attr, data, len);
    if (err) {
        LOG_ERR("Failed to notify asdc characteristic (err %d)", err);
    } else {
        LOG_DBG("Successfully notified %d bytes to asdc characteristic", len);
    }
}

//
// GATT Callbacks
//

ssize_t asdc_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, 
                      const void *buf, uint16_t len, uint16_t offset, 
                      uint8_t flags) {
    // when the central writes to the characteristic
    asdc_on_data_received((uint8_t *)buf, len);
    return len;
}

//
// Define GATT Services and Characteristics for each instance
//

BT_GATT_SERVICE_DEFINE(                                                                         \
    asdc_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(ZMK_BT_ASDC_SERVICE_UUID)),           \
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(ZMK_BT_ASDC_CHAR_UUID),                          \
                        BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,                  \
                        BT_GATT_PERM_WRITE_ENCRYPT,                                             \
                        NULL,                                                                   \
                        asdc_write_cb,                                                          \
                        NULL),                                                                  \
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),                  \
);
