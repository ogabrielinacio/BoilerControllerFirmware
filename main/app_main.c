#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <nvs_flash.h>
#include "wifi_prov_ble.h"
#include "mqtt_tcp.h"
#include <esp_log.h>
#include "esp_spiffs.h"
#include "cJSON.h"
#include "freertos/timers.h"
#include <stdbool.h>

#define RELAY_PIN 25
#define BOILER_ON 1
#define BOILER_OFF 0

int boilerState = 0;
char createTopic[50];
char dataTopic[50];
char liveTopic[50];

void save_id_to_nvs(const char *id)
{
  esp_err_t err;
  nvs_handle_t nvs_handle;

  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK)
  {
    printf("Error opening NVS!\n");
    return;
  }

  err = nvs_set_str(nvs_handle, "device_id", id);
  if (err == ESP_OK)
  {
    printf("ID successfully saved to NVS: %s\n", id);
  }
  else
  {
    printf("Error saving ID to NVS\n");
  }

  nvs_commit(nvs_handle);
  nvs_close(nvs_handle);
}

char *load_id_from_nvs()
{
  esp_err_t err;
  nvs_handle_t nvs_handle;
  char *id = malloc(128);
  if (id == NULL)
  {
    printf("Error allocating memory for ID\n");
    return NULL;
  }

  err = nvs_open("storage", NVS_READONLY, &nvs_handle);
  if (err != ESP_OK)
  {
    printf("Error opening NVS for reading!\n");
    free(id);
    return NULL;
  }

  size_t required_size = 128;
  err = nvs_get_str(nvs_handle, "device_id", id, &required_size);
  if (err == ESP_OK)
  {
    printf("ID loaded from NVS: %s\n", id);
  }
  else if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    printf("ID not found in NVS\n");
  }
  else
  {
    printf("Error loading ID from NVS\n");
  }
  nvs_close(nvs_handle);
  return id;
}

void create_topic_handler(const char *json_message, int data_len)
{
  cJSON *root = cJSON_Parse(json_message);
  if (root == NULL)
  {
    printf("Error parsing JSON\n");
    return;
  }

  cJSON *id_item = cJSON_GetObjectItem(root, "Id");
  if (cJSON_IsString(id_item) && (id_item->valuestring != NULL))
  {
    printf("Received ID: %s\n", id_item->valuestring);
    save_id_to_nvs(id_item->valuestring);
  }
  else
  {
    printf("ID not found or invalid in JSON\n");
  }

  cJSON_Delete(root);
}
void data_topic_handler(const char *json_message, int data_len)
{
  cJSON *root = cJSON_Parse(json_message);
  if (root == NULL)
  {
    printf("Error parsing JSON\n");
    return;
  }

  cJSON *id_item = cJSON_GetObjectItem(root, "id");
  if (!cJSON_IsString(id_item) || id_item->valuestring == NULL)
  {
    printf("Invalid ID in JSON\n");
    cJSON_Delete(root);
    return;
  }

  char *stored_id = load_id_from_nvs();
  if (stored_id == NULL)
  {
    printf("Error loading ID from NVS\n");
    cJSON_Delete(root);
    return;
  }

  if (strcmp(id_item->valuestring, stored_id) == 0)
  {
    printf("ID correspondente encontrado: %s\n", id_item->valuestring);

    cJSON *is_enable_item = cJSON_GetObjectItem(root, "isEnable");
    if (cJSON_IsBool(is_enable_item))
    {
      boilerState = cJSON_IsTrue(is_enable_item) ? BOILER_ON : BOILER_OFF;
      printf("Boiler %s\n", boilerState == BOILER_ON ? "activated" : "deactivated");

      gpio_set_level(RELAY_PIN, boilerState);
    }
    else
    {
      printf("Invalid or missing isEnable in JSON\n");
    }
  }
  else
  {
    printf("Received ID (%s) does not match stored ID (%s)\n", id_item->valuestring, stored_id);
  }

  free(stored_id);
  cJSON_Delete(root);
}

void send_status()
{
  char *stored_id = load_id_from_nvs();
  if (stored_id == NULL)
  {
    printf("Error: Unable to load ID from NVS. Skipping status publish.\n");
    return;
  }

  cJSON *status_json = cJSON_CreateObject();
  if (status_json == NULL)
  {
    printf("Error: Unable to create JSON object.\n");
    free(stored_id);
    return;
  }

  cJSON_AddStringToObject(status_json, "id", stored_id);
  cJSON_AddBoolToObject(status_json, "isLive", true);
  cJSON_AddBoolToObject(status_json, "isEnable", boilerState == BOILER_ON ? true : false);

  char *json_string = cJSON_PrintUnformatted(status_json);
  if (json_string == NULL)
  {
    printf("Error: Unable to convert JSON to string.\n");
    cJSON_Delete(status_json);
    free(stored_id);
    return;
  }

  if (mqtt_tcp_publish(liveTopic, json_string, 0, 0) != ESP_OK)
  {
    printf("Error: Failed to publish to topic %s\n", liveTopic);
  }
  else
  {
    printf("Status sent: %s\n", json_string);
  }

  free(json_string);
  cJSON_Delete(status_json);
  free(stored_id);
}

void my_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  if (event == NULL)
  {
    printf("Error: Null event received.\n");
    return;
  }
  switch (event->event_id)
  {
  case MQTT_EVENT_CONNECTED:
    printf("Connected to MQTT broker.\n");
    break;
  case MQTT_EVENT_DISCONNECTED:
    printf("Disconnected from MQTT broker.\n");
    break;
  case MQTT_EVENT_DATA:

    if (event->topic == NULL || event->data == NULL)
    {
      printf("Error: MQTT event data or topic is NULL.\n");
      break;
    }

    char topic[64] = {0};
    int topic_len = event->topic_len < sizeof(topic) - 1 ? event->topic_len : sizeof(topic) - 1;
    strncpy(topic, event->topic, topic_len);

    char buffer[256] = {0};
    int data_len = event->data_len < sizeof(buffer) - 1 ? event->data_len : sizeof(buffer) - 1;
    strncpy(buffer, event->data, data_len);

    printf("Received message on topic: %.*s\r\n", event->topic_len, event->topic);
    printf("Message data: %.*s\r\n", event->data_len, event->data);
    if (strcmp(topic, createTopic) == 0)
    {
      create_topic_handler(buffer, data_len);
    }
    else if (strcmp(topic, dataTopic) == 0)
    {
      data_topic_handler(buffer, data_len);
    }

    break;
  default:
    printf("Unhandled MQTT event id: %d\n", event->event_id);
    break;
  }
}

void app_main(void)
{
  snprintf(createTopic, sizeof(createTopic), "%s/create", MQTT_BASE_TOPIC);
  snprintf(dataTopic, sizeof(dataTopic), "%s/data", MQTT_BASE_TOPIC);
  snprintf(liveTopic, sizeof(liveTopic), "%s/live", MQTT_BASE_TOPIC);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());

    ESP_ERROR_CHECK(nvs_flash_init());
  }

  gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
  boilerState = BOILER_ON;
  gpio_set_level(RELAY_PIN, boilerState);

  wifi_prov_ble("BoilerCtrl", "abcd1234", "custom-data", 2, true);

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = MQTT_URL,
      .credentials.username = MQTT_USER,
      .credentials.authentication.password = MQTT_PASSWD};

  mqtt_tcp(mqtt_cfg);

  char *stored_id = load_id_from_nvs();
  if (stored_id == NULL)
  {
    if (mqtt_tcp_publish(createTopic, "create", 0, 0) != ESP_OK)
    {
      printf("Error: Failed to publish to topic %s\n", createTopic);
    }
  }
  else
  {
    printf("Device ID already exists in NVS: %s\n", stored_id);
    send_status();
    free(stored_id);
  }

  TimerHandle_t status_timer = xTimerCreate(
      "StatusTimer",        // Timer name
      pdMS_TO_TICKS(60000), // Timer period (1 minute)
      pdTRUE,               // Auto-reload
      NULL,                 // Timer ID (not used)
      send_status           // Timer callback
  );

  if (status_timer == NULL)
  {
    printf("Error: Unable to create status timer.\n");
  }
  else
  {
    xTimerStart(status_timer, 0);
  }

  vTaskDelay(2000 / portTICK_PERIOD_MS);

  if (mqtt_tcp_subscribe(dataTopic, 0) != ESP_OK)
  {
    printf("Error: Failed to subscribe to topic %s\n", dataTopic);
  }

  if (mqtt_tcp_subscribe(createTopic, 0) != ESP_OK)
  {
    printf("Error: Failed to subscribe to topic %s\n", createTopic);
  }

  mqtt_register_event_callback(my_mqtt_event_handler);
}
