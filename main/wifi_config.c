#include "wifi_config.h"

WiFiConfig wifi_config = {
    .ssid = "your_ssid",
    .password = "your_password",

    .use_static_ip = true,

    .static_ip = "X.X.X.X",
    .gateway   = "X.X.X.X",
    .subnet    = "X.X.X.X",
    .dns1      = "X.X.X.X",
    .dns2      = "X.X.X.X"
};
