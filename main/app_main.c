#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <nvs_flash.h>
#include "wifi_prov_ble.h"
#include "mqtt_tcp.h"
#include <esp_log.h>
#include "esp_spiffs.h"
#include "cJSON.h"

#define RELAY_PIN 25

int boilerState = 0;

void my_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
  case MQTT_EVENT_CONNECTED:
    printf("Connected to MQTT broker.\n");
    break;
  case MQTT_EVENT_DISCONNECTED:
    printf("Disconnected from MQTT broker.\n");
    break;
  case MQTT_EVENT_DATA:
    char buffer[32] = {0};
    int copy_len = event->data_len < sizeof(buffer) - 1 ? event->data_len : sizeof(buffer) - 1;
    strncpy(buffer, event->data, copy_len);

    for (int i = 0; i < copy_len; i++)
    {
      buffer[i] = tolower((unsigned char)buffer[i]);
    }
    if (strcmp(buffer, "ligar") == 0 || strcmp(buffer, "on") == 0 || strcmp(buffer, "1") == 0)
    {
      boilerState = 1;
    }
    else if (strcmp(buffer, "desligar") == 0 || strcmp(buffer, "off") == 0 || strcmp(buffer, "0") == 0)
    {
      boilerState = 0;
    }

    gpio_set_level(RELAY_PIN, boilerState);

    if (boilerState == 1)
    {

      mqtt_tcp_publish("data", "Boiler Ligado", 0, 0);
    }
    else
    {

      mqtt_tcp_publish("data", "Boiler Desligado", 0, 0);
    }

    printf("Received message on topic: %.*s\r\n", event->topic_len, event->topic);
    printf("Message data: %.*s\r\n", event->data_len, event->data);
    break;
  default:
    printf("Unhandled MQTT event id: %d\n", event->event_id);
    break;
  }
}

void app_main(void)
{

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());

    ESP_ERROR_CHECK(nvs_flash_init());
  }

  gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);

  wifi_prov_ble("BoilerCtrl", "abcd1234", "custom-data", 2, true);

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = MQTT_URL,
      .credentials.username = MQTT_USER,
      .credentials.authentication.password = MQTT_PASSWD};

  mqtt_tcp(mqtt_cfg);

  mqtt_tcp_publish("data", "Iniciando Placa", 0, 0);

  vTaskDelay(2000 / portTICK_PERIOD_MS);

  gpio_set_level(RELAY_PIN, boilerState);

  vTaskDelay(2000 / portTICK_PERIOD_MS);

  mqtt_tcp_publish("data", "Boiler Desligado", 0, 0);

  mqtt_tcp_subscribe("commands", 0);

  mqtt_register_event_callback(my_mqtt_event_handler);
}
