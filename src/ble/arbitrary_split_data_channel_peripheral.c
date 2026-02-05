
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>

#include <arbitrary_split_data_channel.h>
#include <zmk/events/split_peripheral_status_changed.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void asdc_l2cap_connected(struct bt_l2cap_chan *chan);
static void asdc_l2cap_disconnected(struct bt_l2cap_chan *chan);
static int asdc_l2cap_recv(struct bt_l2cap_chan *chan, struct net_buf *buf);

static struct bt_l2cap_server asdc_l2cap_server;

static struct bt_l2cap_chan_ops asdc_l2cap_ops = {
    .connected = asdc_l2cap_connected,
    .disconnected = asdc_l2cap_disconnected,
    .recv = asdc_l2cap_recv,
};

static struct bt_l2cap_le_chan asdc_l2cap_chan = {
	.chan.ops = &asdc_l2cap_ops,
	.rx.mtu = CONFIG_BT_L2CAP_TX_MTU,
};

// match the CONFIG_ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_TX_QUEUE_SIZE here
NET_BUF_POOL_FIXED_DEFINE(asdc_peripheral_tx_pool, 
                          CONFIG_ZMK_ARBITRARY_SPLIT_DATA_CHANNEL_TX_QUEUE_SIZE, 
                          BT_L2CAP_SDU_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU), 8, NULL);

//
// L2CAP Channel Callbacks
//

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
    
    LOG_DBG("Peripheral L2CAP channel connected: %s, TX MTU %d, RX MTU %d", 
            addr, le_chan->tx.mtu, le_chan->rx.mtu);
}

static void asdc_l2cap_disconnected(struct bt_l2cap_chan *chan) {
    struct bt_conn *conn = chan->conn;
    
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("Peripheral L2CAP channel disconnected: %s", addr);
}

static int asdc_l2cap_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
                              struct bt_l2cap_chan **chan) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("L2CAP accept from %s", addr);

    if (asdc_l2cap_chan.chan.conn) {
        LOG_WRN("L2CAP channel already active, rejecting new connection");
        return -ENOMEM;
    }

    *chan = &asdc_l2cap_chan.chan;
    
    return 0;
}

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
    } else {
        LOG_DBG("ASDC device disconnected");
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(asdc_split_peripheral, asdc_split_peripheral_status_listener);
ZMK_SUBSCRIPTION(asdc_split_peripheral, zmk_split_peripheral_status_changed);

//
// Transport-specific functions
//

int asdc_transport_init(const struct device *dev) {
    // Register L2CAP server
    asdc_l2cap_server.psm = CONFIG_ZMK_BT_ASDC_L2CAP_PSM;
    asdc_l2cap_server.accept = asdc_l2cap_accept;
    asdc_l2cap_server.sec_level = BT_SECURITY_L1;

    int err = bt_l2cap_server_register(&asdc_l2cap_server);
    if (err) {
        LOG_ERR("Failed to register L2CAP server (err %d)", err);
        return err;
    }
    
    LOG_DBG("L2CAP server registered on PSM 0x%04x", CONFIG_ZMK_BT_ASDC_L2CAP_PSM);
    return 0;
}

void asdc_transport_send_data(const struct device *dev, const uint8_t *data, size_t length) {
    
    if (!asdc_l2cap_chan.chan.conn) {
        LOG_ERR("No active L2CAP channel for ASDC data send");
        return;
    }

    if (length > CONFIG_BT_L2CAP_TX_MTU) {
        LOG_ERR("Length %zu exceeds configured MTU %d", length, CONFIG_BT_L2CAP_TX_MTU);
        return;
    }
    
    if (length > asdc_l2cap_chan.tx.mtu) {
        LOG_ERR("Length %zu exceeds negotiated TX MTU %d", length, asdc_l2cap_chan.tx.mtu);
        return;
    }

    struct net_buf *buf = net_buf_alloc(&asdc_peripheral_tx_pool, K_SECONDS(2));
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

    int err = bt_l2cap_chan_send(&asdc_l2cap_chan.chan, buf);
    if (err < 0) {
        LOG_ERR("Failed to send L2CAP data (err %d)", err);
        net_buf_unref(buf);
    }
}
