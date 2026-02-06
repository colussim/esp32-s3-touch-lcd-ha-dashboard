#include "wifi_config.h"

WiFiConfig wifi_config = {
    .ssid = "Gen_home2",
    .password = "zinc792_hart",

    .use_static_ip = true,

    .static_ip = "192.168.0.19",
    .gateway   = "192.168.0.254",
    .subnet    = "255.255.255.0",
    .dns1      = "192.168.0.254",
    .dns2      = "8.8.8.8"
};
