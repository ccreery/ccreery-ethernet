/*
 * Copyright (c) 2014-2017 Cesanta Software Limited
 * All rights reserved
 */

#include <stdbool.h>

#include "esp_eth.h"
#include "eth_phy/phy_lan8720.h"
#include "eth_phy/phy_tlk110.h"
#include "tcpip_adapter.h"

#include "lwip/ip_addr.h"

#include "mgos_eth.h"
#include "mgos_net.h"
#include "mgos_net_hal.h"
#include "mgos_sys_config.h"

static void eth_config_pins(void) {
    phy_rmii_configure_data_interface_pins();
    phy_rmii_smi_configure_pins(mgos_sys_config_get_eth_mdc_gpio(), mgos_sys_config_get_eth_mdio_gpio());
}

#define PIN_PHY_POWER 17
static void phy_device_power_enable_via_gpio(eth_config_t *config, bool enable) {
    assert(config->phy_power_enable);

    if (!enable) {
        /* Do the PHY-specific power_enable(false) function before powering down */
        config->phy_power_enable(false);
    }

    gpio_pad_select_gpio(PIN_PHY_POWER);
    gpio_set_direction(PIN_PHY_POWER, GPIO_MODE_OUTPUT);
    if (enable == true) {
        gpio_set_level(PIN_PHY_POWER, 1);
        LOG(LL_INFO, ("phy_device_power_enable(TRUE)"));
    } else {
        gpio_set_level(PIN_PHY_POWER, 0);
        LOG(LL_INFO, ("power_enable(FALSE)"));
    }

    // Allow the power up/down to take effect, min 300us
    vTaskDelay(1);

    if (enable) {
        // Run the PHY-specific power on operations now the PHY has power
        config->phy_power_enable(true);
    }
}

bool mgos_ethernet_init(void) {
    bool res = false;

    LOG(LL_INFO, ("fee fie foe fum"));

    if (!mgos_sys_config_get_eth_enable()) {
        res = true;
        goto clean;
    }

    tcpip_adapter_ip_info_t static_ip;
    if (!mgos_eth_get_static_ip_config(&static_ip.ip, &static_ip.netmask, &static_ip.gw)) {
        goto clean;
    }

    eth_config_t config;
    const char *phy_model;
#if defined(MGOS_ETH_PHY_LAN87x0)
    phy_model = "LAN87x0";
    config = phy_lan8720_default_ethernet_config;
#elif defined(MGOS_ETH_PHY_TLK110)
    phy_model = "TLK110";
    config = phy_tlk110_default_ethernet_config;
#else
#error Unknown/unspecified PHY model
#endif

    /* Set the PHY address in the example configuration */
    config.phy_addr = mgos_sys_config_get_eth_phy_addr();
    config.gpio_config = eth_config_pins;
    config.tcpip_input = tcpip_adapter_eth_input;
    config.phy_power_enable = phy_device_power_enable_via_gpio;

    esp_err_t ret = esp_eth_init(&config);
    if (ret != ESP_OK) {
        LOG(LL_ERROR, ("Ethernet init failed: %d", ret));
        return false;
    }

    uint8_t mac[6];
    esp_eth_get_mac(mac);
    bool is_dhcp = ip4_addr_isany_val(static_ip.ip);

    LOG(LL_INFO, ("ETH: MAC %02x:%02x:%02x:%02x:%02x:%02x; PHY: %s @ %d%s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], phy_model,
                  mgos_sys_config_get_eth_phy_addr(), (is_dhcp ? "; IP: DHCP" : "")));
    if (!is_dhcp) {
        char ips[16], nms[16], gws[16];
        ip4addr_ntoa_r(&static_ip.ip, ips, sizeof(ips));
        ip4addr_ntoa_r(&static_ip.netmask, nms, sizeof(nms));
        ip4addr_ntoa_r(&static_ip.gw, gws, sizeof(gws));
        LOG(LL_INFO, ("ETH: IP %s/%s, GW %s", ips, nms, gws));
        tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_ETH);
        if (tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_ETH, &static_ip) != ESP_OK) {
            LOG(LL_ERROR, ("ETH: Failed to set ip info"));
            goto clean;
        }
    }

    res = (esp_eth_enable() == ESP_OK);

clean:
    return res;
}

bool mgos_eth_dev_get_ip_info(int if_instance, struct mgos_net_ip_info *ip_info) {
    tcpip_adapter_ip_info_t info;
    if (if_instance != 0 || tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    ip_info->ip.sin_addr.s_addr = info.ip.addr;
    ip_info->netmask.sin_addr.s_addr = info.netmask.addr;
    ip_info->gw.sin_addr.s_addr = info.gw.addr;
    return true;
}
