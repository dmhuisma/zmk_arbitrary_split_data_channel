
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

#include <arbitrary_split_data_channel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct asdc_peripheral_slot {
    struct bt_conn* conn;
    struct bt_l2cap_le_chan chan;
};

static struct asdc_peripheral_slot peripheral_slots[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];

NET_BUF_POOL_FIXED_DEFINE(asdc_central_tx_pool, 5, BT_L2CAP_SDU_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU), 8, NULL);

static int asdc_l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf) {    
    if (buf->len > 0) {
        asdc_on_data_received(chan->conn, buf->data, buf->len);
    }
    return 0;
}

static void asdc_l2cap_connected(struct bt_l2cap_chan *chan) {
    struct bt_l2cap_le_chan *le_chan = BT_L2CAP_LE_CHAN(chan);
    struct bt_conn *conn = chan->conn;
    
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    
    LOG_DBG("L2CAP channel connected: %s, TX MTU %d, RX MTU %d", 
            addr, le_chan->tx.mtu, le_chan->rx.mtu);
}

static void asdc_l2cap_disconnected(struct bt_l2cap_chan *chan) {
    struct bt_conn *conn = chan->conn;
    
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("L2CAP channel disconnected: %s", addr);
}

static struct bt_l2cap_chan_ops asdc_l2cap_ops = {
    .connected = asdc_l2cap_connected,
    .disconnected = asdc_l2cap_disconnected,
    .recv = asdc_l2cap_recv,
};

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
    
    // Connect L2CAP channel
    struct bt_l2cap_le_chan *le_chan = &peripheral_slots[i].chan;
    LOG_DBG("Connecting L2CAP channel to PSM 0x%04x", CONFIG_ZMK_BT_ASDC_L2CAP_PSM);
    int l2cap_err = bt_l2cap_chan_connect(conn, &le_chan->chan, CONFIG_ZMK_BT_ASDC_L2CAP_PSM);
    if (l2cap_err) {
        LOG_ERR("Failed to connect L2CAP channel (err %d)", l2cap_err);
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
            break;
        }
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

//
// Transport-specific functions
//

int asdc_transport_init(const struct device *dev) {

    for (uint8_t i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        peripheral_slots[i].chan.chan.ops = &asdc_l2cap_ops;
        peripheral_slots[i].chan.rx.mtu = CONFIG_BT_L2CAP_TX_MTU;
    }
    return 0;
}

void asdc_transport_send_data(const struct device *dev, const uint8_t *data, size_t length) {  
    
    if (length > CONFIG_BT_L2CAP_TX_MTU) {
        LOG_ERR("Length %zu exceeds configured MTU %d", length, CONFIG_BT_L2CAP_TX_MTU);
        return;
    }

    // send the data for every peripheral connected to the central
    for (uint8_t i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        struct asdc_peripheral_slot *slot = &peripheral_slots[i];
        
        if (!slot->conn) {
            // no peripheral connected in this slot
            continue;
        }

        if (!slot->chan.chan.conn) {
            LOG_ERR("No active L2CAP channel for ASDC data send");
            continue;
        }

        if (length > slot->chan.tx.mtu) {
            LOG_ERR("Length %zu exceeds negotiated TX MTU %d", length, slot->chan.tx.mtu);
            return;
        }

        // delay in between sending to multiple peripherals
        // to avoid overwhelming the BLE stack
        if (i > 0) {
            k_msleep(100);
        }

        struct net_buf *buf = net_buf_alloc(&asdc_central_tx_pool, K_SECONDS(2));
        if (!buf) {
            LOG_ERR("Failed to allocate net_buf for L2CAP send");
            return;
        }

        net_buf_reserve(buf, BT_L2CAP_SDU_CHAN_SEND_RESERVE);
        
        if (length > net_buf_tailroom(buf)) {
            LOG_ERR("Data too large for buffer (%zu > %d)", length, net_buf_tailroom(buf));
            net_buf_unref(buf);
            return;
        }
        
        net_buf_add_mem(buf, data, length);

        int err = bt_l2cap_chan_send(&slot->chan.chan, buf);
        if (err < 0) {
            LOG_ERR("Failed to send L2CAP data (err %d)", err);
            net_buf_unref(buf);
        }
    }
}
