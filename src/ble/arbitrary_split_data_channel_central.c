
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

#include <arbitrary_split_data_channel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct asdc_peripheral_slot {
    struct bt_conn* conn;
    struct bt_gatt_discover_params discover_params;
    struct bt_gatt_subscribe_params subscribe_params;
    uint16_t asdc_char_handle;
};

static struct asdc_peripheral_slot peripheral_slots[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];

static const struct bt_uuid_128 asdc_char_uuid = BT_UUID_INIT_128(ZMK_BT_ASDC_CHAR_UUID);
static const struct bt_uuid* descriptor_uuid = BT_UUID_GATT_CCC;

static int asdc_peripheral_slot_index_for_conn(struct bt_conn *conn) {
    for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (peripheral_slots[i].conn == conn) {
            return i;
        }
    }
    return -EINVAL;
}

static struct asdc_peripheral_slot *asdc_peripheral_slot_for_conn(struct bt_conn *conn) {
    int idx = asdc_peripheral_slot_index_for_conn(conn);
    if (idx < 0) {
        return NULL;
    }
    return &peripheral_slots[idx];
}

static uint8_t asdc_central_notify_func(struct bt_conn *conn,
                                        struct bt_gatt_subscribe_params *params,
                                        const void *data, uint16_t length) {
    if (!data) {
		printk("asdc notification unsubscribed\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

    asdc_on_data_received((uint8_t *)data, length);

    return BT_GATT_ITER_CONTINUE;
}

static uint8_t asdc_central_descriptor_discovery_func(struct bt_conn *conn,
                                                      const struct bt_gatt_attr *attr,
                                                      struct bt_gatt_discover_params *params) {
    struct asdc_peripheral_slot *slot = asdc_peripheral_slot_for_conn(conn);
    if (slot == NULL) {
        LOG_ERR("No asdc peripheral slot found for connection");
        return BT_GATT_ITER_STOP;
    }

    // found the descriptor handle, now subscribe to notifications
    slot->subscribe_params.notify = asdc_central_notify_func;
    slot->subscribe_params.value = BT_GATT_CCC_NOTIFY;
    slot->subscribe_params.ccc_handle = attr->handle;

    int err = bt_gatt_subscribe(conn, &slot->subscribe_params);
    if (err && err != -EALREADY) {
        LOG_ERR("asdc subscribe failed (err %d)", err);
    } else {
        LOG_DBG("asdc subscribed");
    }
    
    return BT_GATT_ITER_STOP;
}

static uint8_t asdc_central_char_discovery_func(struct bt_conn *conn,
                                                const struct bt_gatt_attr *attr,
                                                struct bt_gatt_discover_params *params) {
    struct asdc_peripheral_slot *slot = asdc_peripheral_slot_for_conn(conn);
    if (slot == NULL) {
        LOG_ERR("No asdc peripheral slot found for connection");
        return BT_GATT_ITER_STOP;
    }

    slot->asdc_char_handle = bt_gatt_attr_value_handle(attr);
    LOG_DBG("asdc char value handle %u", slot->asdc_char_handle);

    // start discovery
    slot->discover_params.uuid = descriptor_uuid;
    slot->discover_params.func = asdc_central_descriptor_discovery_func;
    slot->discover_params.start_handle = attr->handle + 2;
    slot->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    slot->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
    slot->subscribe_params.value_handle = slot->asdc_char_handle;

    LOG_DBG("starting asdc descriptor discovery");
    int err = bt_gatt_discover(conn, &slot->discover_params);
    if (err) {
        LOG_ERR("asdc char discovery failed (err %d)", err);
        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_STOP;
}

//
// Bluetooth connection callbacks
//

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
		return;
	}

	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("asdc connection callback: %s", addr);

    // Store the connection in the peripherals array
    uint8_t i;
    for (i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (peripheral_slots[i].conn == NULL) {
            peripheral_slots[i].conn = conn;
            break;
        }
        if (peripheral_slots[i].conn == conn) {
            break;
        }
        if (i == CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS - 1) {
            LOG_WRN("No space to store new asdc peripheral connection");
            return;
        }
    }

    // Store connection in all ASDC device instances
    asdc_store_connection(conn);

    // start discovery
    peripheral_slots[i].discover_params.uuid = &asdc_char_uuid.uuid;
    peripheral_slots[i].discover_params.func = asdc_central_char_discovery_func;
    peripheral_slots[i].discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    peripheral_slots[i].discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    peripheral_slots[i].discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    LOG_DBG("starting asdc char discovery");
    int discover_err = bt_gatt_discover(conn, &peripheral_slots[i].discover_params);
    if (discover_err) {
        LOG_ERR("asdc char discovery failed (err %d)", discover_err);
        return;
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_DBG("asdc disconnection callback: %s", addr);

    // Remove the connection from the peripherals array
    for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (peripheral_slots[i].conn == conn) {
            peripheral_slots[i].conn = NULL;
            peripheral_slots[i].asdc_char_handle = 0;
            break;
        }
    }

    // Clear connection from all ASDC device instances
    asdc_clear_connection(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

//
// Transport-specific functions
//

int asdc_transport_init(const struct device *dev) {
    return 0;
}

void asdc_transport_send_data(const struct device *dev, const uint8_t *data, size_t len) {
    struct asdc_data *asdc_data = (struct asdc_data *)dev->data;
    struct bt_conn* conn = asdc_data->conn;
    if (!conn) {
        LOG_ERR("No active connection for ASDC data send");
        return;
    }

    struct asdc_peripheral_slot *slot = asdc_peripheral_slot_for_conn(conn);
    if (!slot) {
        LOG_ERR("No peripheral slot found for connection");
        return;
    }

    if (slot->asdc_char_handle == 0) {
        LOG_ERR("ASDC characteristic handle not discovered yet");
        return;
    }

    int err = bt_gatt_write_without_response(conn, slot->asdc_char_handle, data, len, true);
    if (err) {
        LOG_ERR("Failed to write asdc characteristic (err %d)", err);
    } else {
        LOG_DBG("Successfully wrote %d bytes to asdc characteristic handle %d", len, slot->asdc_char_handle);
    }
}
