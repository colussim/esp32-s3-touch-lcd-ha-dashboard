#include "mqtt_config.h"

const MqttConfig mqtt_config = {
  .host = "X.X.X.X",
  .port = 1883,
  .user = "mqtt_user",
  .pass = "mqtt_password",

  .base = "home/roo1panel",

  .topic_left_cmd    = "home/roo1panel/lamp_left/cmd",
  .topic_left_state  = "home/roo1panel/lamp_left/state",
  .topic_right_cmd   = "home/roo1panel/lamp_right/cmd",
  .topic_right_state = "home/roo1panel/lamp_right/state",
  .topic_temperature = "home/roo1panel/temperature", 

  .topic_status = "home/roo1panel/status", 

};

