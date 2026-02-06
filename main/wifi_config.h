#pragma once
#include <stdbool.h>

typedef struct {
    const char *ssid;
    const char *password;

    bool use_static_ip;

    const char *static_ip;
    const char *gateway;
    const char *subnet;
    const char *dns1;
    const char *dns2;
} WiFiConfig;

extern WiFiConfig wifi_config;
