#pragma once

typedef struct {
  const char *host;
  int port;
  const char *user;
  const char *pass;

  const char *base;

  const char *topic_left_cmd;
  const char *topic_left_state;
  const char *topic_right_cmd;
  const char *topic_right_state;
  const char *topic_temperature;

  const char *topic_status;
} MqttConfig;

extern const MqttConfig mqtt_config;