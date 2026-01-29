# ZMK Arbitrary Split Data Channel

This is a [ZMK](https://zmk.dev) module that adds a communication channel between split devices. It is intended to be used as a dependency in other modules. It allows for the sending of any arbitrary data between the split devices, so that you are not limited by the information provided by ZMK's split transport API.

My inspiration for this module was trying to find something useful to do with the display on my keyboard after migrating to using a dongle. Once the keyboard was no longer a central device, the information available to it to display was rather limited. This module could act as a relay so it would now be possible to display, for example, the battery levels of all peripherals or the current layer without requiring a display on the dongle which can now be hidden away instead of taking extra space on a desk. The display module would need to explicitly add support for this using this module.

It currently implements the BLE split transport. Other transports (wire, ESB) are currently not implemented but it might be possible to do so by implementing them in the src/(type of transport) subdirectories.

# How to use

> [!NOTE]
> Note: this has not been tested on ZMK 0.4 yet.

Include this project on your ZMK's west manifest in config/west.yml:

```diff
  [...]
  remotes:
+    - name: dmhuisma
+      url-base: https://github.com/dmhuisma
  projects:
+    - name: zmk_arbitrary_split_data_channel
+      remote: dmhuisma
+      revision: main
  [...]
```
Add the node to your overlay file. You can have multiple channels, each channel should have a unique channel-id. Add it as a dependency in another module. An example of a consumer module is included in the "example_consumer" directory.

``` c
/{
    sdc0: split_data_channel {
        compatible = "zmk,arbitrary-split-data-channel";
        channel-id = <1>;
        status = "okay";
    };

    sdcc0: split_data_channel_consumer {
        compatible = "zmk,arbitrary-split-data-channel-consumer";
        // you can specify multiple channels here
        asdc-channels = <&sdc0>;
        status = "okay";
    };
}
```

The BLE implementation uses L2CAP, which can allow for data sizes larger than what would otherwise be available in BLE. You have to make sure to set you kconfig settings accordingly. For example, the example_consumer sends a message that is about 400 bytes in size. The following settings are sufficient for this.

```
# L2CAP MTU configuration - set to at least your packet size
CONFIG_BT_L2CAP_TX_MTU=512

# ACL buffer sizes - must be >= L2CAP_TX_MTU + L2CAP overhead (~4-8 bytes)
CONFIG_BT_BUF_ACL_TX_SIZE=517
CONFIG_BT_BUF_ACL_RX_SIZE=517

# Controller data length extension (251 is max for BLE, fragmentation handles larger)
CONFIG_BT_CTLR_DATA_LENGTH_MAX=251

CONFIG_BT_L2CAP_DYNAMIC_CHANNEL=y

# Increase overall buffer counts
CONFIG_BT_BUF_ACL_TX_COUNT=8
CONFIG_BT_BUF_ACL_RX_COUNT=8
```
