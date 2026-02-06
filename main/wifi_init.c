#include <string.h>

#include "lwip/ip4_addr.h"
#include "lwip/inet.h"      

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "wifi_config.h"

static const char *TAG = "wifi";

/* --------- WIFI CONNECTED BIT (IP READY) --------- */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

void wifi_wait_connected(void)
{
    if (!s_wifi_event_group) return;
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

/* --------- helpers --------- */
static bool parse_ip4(const char *s, esp_ip4_addr_t *out)
{
    ip4_addr_t tmp;
    if (!ip4addr_aton(s, &tmp)) return false;
    out->addr = tmp.addr;
    return true;
}

/* --------- event handler --------- */
static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start -> connect");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected — retrying");
        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* e = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));

    
        esp_wifi_set_ps(WIFI_PS_NONE);

        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

/* --------- main init --------- */
void wifi_init_sta(void)
{
    ESP_LOGI(TAG, "Init WiFi");

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    if (wifi_config.use_static_ip) {
        esp_netif_ip_info_t ip = {0};

        if (!parse_ip4(wifi_config.static_ip, &ip.ip) ||
            !parse_ip4(wifi_config.gateway,   &ip.gw) ||
            !parse_ip4(wifi_config.subnet,    &ip.netmask)) {
            ESP_LOGE(TAG, "Invalid static IP config (check static_ip/gateway/subnet)");
        } else {
            ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
            ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip));

            esp_netif_dns_info_t dns = {0};

            // DNS1
            if (wifi_config.dns1 && parse_ip4(wifi_config.dns1, &dns.ip.u_addr.ip4)) {
                dns.ip.type = IPADDR_TYPE_V4;
                ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns));
            }

            // DNS2
            if (wifi_config.dns2 && parse_ip4(wifi_config.dns2, &dns.ip.u_addr.ip4)) {
                dns.ip.type = IPADDR_TYPE_V4;
                ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns));
            }

            ESP_LOGI(TAG, "Static IP configured: %s", wifi_config.static_ip);
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    wifi_config_t sta = {0};

    strncpy((char*)sta.sta.ssid, wifi_config.ssid, sizeof(sta.sta.ssid));
    strncpy((char*)sta.sta.password, wifi_config.password, sizeof(sta.sta.password));

    
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
}