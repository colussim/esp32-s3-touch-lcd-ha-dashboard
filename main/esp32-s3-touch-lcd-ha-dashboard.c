#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "mqtt_client.h"

#include "esp_sntp.h"
#include <time.h>

#include "esp_timer.h"
#include "bsp/display.h" 

// BSP Waveshare + LVGL
#include "bsp/esp32_s3_touch_lcd_4.h"
#include "lvgl.h"

#include "mqtt_config.h"
#include "lamp_config.h"

static void screen_reset_timeout(void);
static void screen_touch_cb(lv_event_t *e);

// WiFi 
extern void wifi_init_sta(void);
extern void wifi_wait_connected(void);


static const char *TAG = "ws_lcd4";

// ---- UI Globals ----
static lv_obj_t *label_clock;
static lv_obj_t *btn_left;
static lv_obj_t *btn_right;
static lv_obj_t *label_temp;

static bool left_state  = false;
static bool right_state = false;

// Styles OFF/ON 
static lv_style_t style_off;
static lv_style_t style_on;
static bool styles_inited = false;

static esp_mqtt_client_handle_t g_mqtt = NULL;

static char s_mqtt_uri[96];


#define ICON_LAMP "\uE21E" // Unicode Material Icons: lamp
#define COLOR_LAMP_ON   lv_color_hex(0xFF9800)
#define COLOR_LAMP_OFF  lv_color_hex(0x607D8B)

#define SCREEN_TIMEOUT_MS  (120000)  // 2 minutes


LV_IMAGE_DECLARE(floor_lamp); 
LV_IMG_DECLARE(backg_room1); 
LV_IMG_DECLARE(ui_img_clock_icon);
LV_IMG_DECLARE(ui_thermostat_icon);

static esp_timer_handle_t screen_timer;
static bool screen_dimmed = false;


// ---------------- UI helpers ----------------
static void init_button_styles_once(void)
{
    if (styles_inited) return;
    styles_inited = true;

    lv_style_init(&style_off);
    lv_style_set_bg_color(&style_off, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_off, LV_OPA_COVER);
    lv_style_set_radius(&style_off, 10);

    lv_style_init(&style_on);
    lv_style_set_bg_color(&style_on, COLOR_LAMP_ON);
    lv_style_set_bg_opa(&style_on, LV_OPA_COVER);
    lv_style_set_radius(&style_on, 10);
}

static void set_btn_state(lv_obj_t *btn, bool on)
{
    if (on) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(btn, COLOR_LAMP_ON, LV_PART_MAIN);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(btn, COLOR_LAMP_OFF, LV_PART_MAIN);
    }
}

static void ui_set_left(bool on)
{
    if (on == left_state) return;
    left_state = on;

    bsp_display_lock(0);
    set_btn_state(btn_left, left_state);
    bsp_display_unlock();
}

static void ui_set_right(bool on)
{
    if (on == right_state) return;
    right_state = on;

    bsp_display_lock(0);
    set_btn_state(btn_right, right_state);
    bsp_display_unlock();
}

static inline void mqtt_publish(const char *topic, const char *payload, int qos, int retain)
{
    if (!g_mqtt || !topic) return;
    esp_mqtt_client_publish(g_mqtt, topic, payload, 0, qos, retain);
}

static inline void send_toggle_left(void)
{
    mqtt_publish(mqtt_config.topic_left_cmd, "TOGGLE", 1, 0);
}

static inline void send_toggle_right(void)
{
    mqtt_publish(mqtt_config.topic_right_cmd, "TOGGLE", 1, 0);
}

// ---------------- Button callback ----------------
static void btn_event_cb(lv_event_t *e)
{
    screen_reset_timeout();
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    const char *name = (const char *)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Button clicked: %s", name);

    if (strcmp(name, "lamp_left") == 0) {
        send_toggle_left();
    } else if (strcmp(name, "lamp_right") == 0) {
        send_toggle_right();
    }
}

// ---------------- UI create ----------------
static void ui_create(void)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *scr = lv_screen_active();
#else
    lv_obj_t *scr = lv_scr_act();
#endif

    // --- ADDING THE BACKGROUND IMAGE ---
    // We create the image first so that it is in the background.
    lv_obj_t * bg = lv_img_create(scr);
    lv_img_set_src(bg, &backg_room1);
    lv_obj_center(bg); 

    init_button_styles_once();

     // --- CREAT CREATION OF THE CLOCK BADGE ---
    lv_obj_t * clock_badge = lv_obj_create(scr);
    lv_obj_set_size(clock_badge, 100, 40);
    lv_obj_align(clock_badge, LV_ALIGN_TOP_MID, 0, 10);
    
    // 1. Apply the COLOR_LAMP_OFF color
    lv_obj_set_style_bg_color(clock_badge, lv_color_hex(0x607D8B), 0);
    lv_obj_set_style_bg_opa(clock_badge, LV_OPA_COVER, 0); // Make the background fully opaque
    
    // 2. Optional: Harmonize borders and rounding
    lv_obj_set_style_border_width(clock_badge, 0, 0);      // Remove the default border
    lv_obj_set_style_radius(clock_badge, 10, 0);           // Rounded corners matching the buttons
    
    lv_obj_set_style_pad_all(clock_badge, 5, 0);
    lv_obj_set_layout(clock_badge, 0); 
    lv_obj_clear_flag(clock_badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icon_clock = lv_img_create(clock_badge);
    lv_img_set_src(icon_clock, &ui_img_clock_icon);
    lv_obj_align(icon_clock, LV_ALIGN_LEFT_MID, 8, 0);

    label_clock = lv_label_create(clock_badge);
    lv_label_set_text(label_clock, "--:--");
    lv_obj_set_style_text_color(label_clock, lv_color_white(), 0); // Text in white
    lv_obj_set_style_text_font(label_clock, &lv_font_montserrat_14, 0); 
    lv_obj_align(label_clock, LV_ALIGN_RIGHT_MID, -10, 0);

    // =========================
    // Left button
    // =========================
    btn_left = lv_btn_create(scr);
    lv_obj_set_size(btn_left, 200, 130);
    lv_obj_align(btn_left, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_add_event_cb(btn_left, btn_event_cb, LV_EVENT_CLICKED, (void*)"lamp_left");

    lv_obj_add_style(btn_left, &style_off, 0);
    lv_obj_add_style(btn_left, &style_on, LV_STATE_CHECKED);

    lv_obj_t *icon_left = lv_image_create(btn_left);
    lv_image_set_src(icon_left, &floor_lamp);
    lv_obj_align(icon_left, LV_ALIGN_TOP_MID, 0, 8);
  
    // Label (bottom)
    lv_obj_t *text_left = lv_label_create(btn_left);
    lv_label_set_text(text_left, lamp_config.left_label);
    lv_obj_set_style_text_align(text_left, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(text_left, LV_ALIGN_BOTTOM_MID, 0, -10);


    // =========================
    // Right button
    // =========================
    btn_right = lv_btn_create(scr);

    lv_obj_set_size(btn_right, 200, 130);
    lv_obj_align(btn_right, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(btn_right, btn_event_cb, LV_EVENT_CLICKED, (void*)"lamp_right");

    lv_obj_add_style(btn_right, &style_off, 0);
    lv_obj_add_style(btn_right, &style_on, LV_STATE_CHECKED);

    lv_obj_t *icon_right = lv_image_create(btn_right);
    lv_image_set_src(icon_right, &floor_lamp); 
    lv_obj_align(icon_right, LV_ALIGN_TOP_MID, 0, 8);

    // Label (bottom)
    lv_obj_t *text_right = lv_label_create(btn_right);
    lv_label_set_text(text_right, lamp_config.right_label);
    lv_obj_set_style_text_align(text_right, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(text_right, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Initial states (will be updated by MQTT retain)
    set_btn_state(btn_left, left_state);
    set_btn_state(btn_right, right_state);

    
    // --- CREATION OF THE TEMPERATURE BADGE ---
    lv_obj_t * temp_badge = lv_obj_create(scr);
    lv_obj_set_size(temp_badge, 100, 40); 
    lv_obj_align(temp_badge, LV_ALIGN_BOTTOM_MID, 0, -15); // A bit higher from the edge
    
    // Style of the badge (Color 0x607D8B consistent with the buttons)
    lv_obj_set_style_bg_color(temp_badge, lv_color_hex(0x607D8B), 0);
    lv_obj_set_style_bg_opa(temp_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(temp_badge, 0, 0);
    lv_obj_set_style_radius(temp_badge, 10, 0);
    lv_obj_set_style_pad_all(temp_badge, 5, 0);
    lv_obj_set_layout(temp_badge, 0); 
    lv_obj_clear_flag(temp_badge, LV_OBJ_FLAG_SCROLLABLE);

    // Add thermostat icon
    lv_obj_t * icon_temp = lv_img_create(temp_badge);
    lv_img_set_src(icon_temp, &ui_thermostat_icon);
    lv_obj_align(icon_temp, LV_ALIGN_LEFT_MID, 5, 0);

    // Temperature label (Uses the global variable label_temp)
    label_temp = lv_label_create(temp_badge);
    lv_label_set_text(label_temp, "--.- °C");
    
    lv_obj_set_style_text_color(label_temp, lv_color_white(), 0); 
    lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_14, 0); 
    
    lv_obj_align(label_temp, LV_ALIGN_RIGHT_MID, -8, 0);

    // --- SCREEN TOUCH EVENT TO RESET TIMEOUT ---
    #if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *scr2 = lv_screen_active();
    #else
    lv_obj_t *scr2 = lv_scr_act();
    #endif
    lv_obj_add_event_cb(scr2, screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr2, screen_touch_cb, LV_EVENT_CLICKED, NULL);
}
// ---------------- MQTT handling ----------------
static void handle_state_msg(const char *topic, const char *data, int len)
{
    bool on = (len >= 2 && data[0] == 'O' && data[1] == 'N');

    if (mqtt_config.topic_left_state && strcmp(topic, mqtt_config.topic_left_state) == 0) {
        ui_set_left(on);
        return;
    }
    if (mqtt_config.topic_right_state && strcmp(topic, mqtt_config.topic_right_state) == 0) {
        ui_set_right(on);
        return;
    }
   
    if (mqtt_config.topic_temperature && strcmp(topic, mqtt_config.topic_temperature) == 0) {
        if (data == NULL || len <= 0) return;

        char buf[16];
        int copy_len = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
        memcpy(buf, data, copy_len);
        buf[copy_len] = '\0';

        ESP_LOGI(TAG, "Affichage Temp: %s", buf);

        bsp_display_lock(0);
        if (label_temp != NULL) {
            lv_label_set_text_fmt(label_temp, "%s °C", buf);
        }
        bsp_display_unlock();
    }
   
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");

        if (mqtt_config.topic_left_state)  esp_mqtt_client_subscribe(g_mqtt, mqtt_config.topic_left_state, 1);
        if (mqtt_config.topic_right_state) esp_mqtt_client_subscribe(g_mqtt, mqtt_config.topic_right_state, 1);
       /* if (mqtt_config.topic_temperature) esp_mqtt_client_subscribe(g_mqtt, mqtt_config.topic_temperature, 1);*/

         if (mqtt_config.topic_temperature) {
      //  ESP_LOGI(TAG, "Sub: %s", mqtt_config.topic_temperature);
        esp_mqtt_client_subscribe(g_mqtt, mqtt_config.topic_temperature, 1);
    }

        // online (retain)
        if (mqtt_config.topic_status) mqtt_publish(mqtt_config.topic_status, "online", 1, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA: {
       char t[192];
        int tn = (e->topic_len < (int)sizeof(t) - 1) ? e->topic_len : (int)sizeof(t) - 1;
        memcpy(t, e->topic, tn);
        t[tn] = 0;

        ESP_LOGI(TAG, "MQTT rx topic=%s data=%.*s", t, e->data_len, e->data);

        // 2. On envoie tout à handle_state_msg (qui gère maintenant lampes ET température)
        handle_state_msg(t, e->data, e->data_len);
        break;
    }

    default:
        break;
    }
}

static void mqtt_start(void)
{
    // Construire mqtt://host:port
    snprintf(s_mqtt_uri, sizeof(s_mqtt_uri), "mqtt://%s:%d", mqtt_config.host, mqtt_config.port);
    ESP_LOGI(TAG, "MQTT URI: %s", s_mqtt_uri);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_mqtt_uri,
        .credentials.username = (mqtt_config.user && mqtt_config.user[0]) ? mqtt_config.user : NULL,
        .credentials.authentication.password = (mqtt_config.pass && mqtt_config.pass[0]) ? mqtt_config.pass : NULL,

        // LWT offline (retain)
        .session.last_will.topic  = mqtt_config.topic_status,
        .session.last_will.msg    = "offline",
        .session.last_will.qos    = 1,
        .session.last_will.retain = 1,

        .network.timeout_ms = 4000,
        .network.disable_auto_reconnect = false,
    };

    g_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(g_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_mqtt);
}

void init_clock_sync() {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    
    // Définir le fuseau horaire (Ex: France/Paris avec heure d'été auto)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void update_clock_label() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Si l'année est < 2020, l'heure n'est pas encore synchronisée
    if (timeinfo.tm_year > 120) {
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        lv_label_set_text(label_clock, buf);
    }
}

static void lcd_sleep(void)
{
    if (screen_dimmed) return;
    screen_dimmed = true;

    // Éteint le rétroéclairage via BSP (0%)
    bsp_display_backlight_off();
}

static void lcd_wake(void)
{
    if (!screen_dimmed) return;
    screen_dimmed = false;

    // Rallume le rétroéclairage via BSP (100%)
    bsp_display_backlight_on();

    // Optionnel: invalide l'écran pour forcer un redraw
    bsp_display_lock(0);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_invalidate(lv_screen_active());
#else
    lv_obj_invalidate(lv_scr_act());
#endif
    bsp_display_unlock();
}

static void screen_timer_cb(void *arg)
{
    (void)arg;
    lcd_sleep();
}

static void screen_reset_timeout(void)
{
    // toute activité => wake + repousse le timer
    lcd_wake();
    esp_timer_stop(screen_timer);
    esp_timer_start_once(screen_timer, (uint64_t)SCREEN_TIMEOUT_MS * 1000ULL);
}

static void screen_touch_cb(lv_event_t *e)
{
    (void)e;
    screen_reset_timeout();
}

// ---------------- Main ----------------
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_sta();
    wifi_wait_connected();

    bsp_display_start();
    bsp_display_backlight_on();   // important au boot

    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    // ---- timer veille écran (AVANT while) ----
    const esp_timer_create_args_t targs = {
        .callback = &screen_timer_cb,
        .name = "screen_timeout",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &screen_timer));
    screen_reset_timeout();  // démarre le countdown 2 min

    init_clock_sync();
    mqtt_start();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        update_clock_label();
    }
}