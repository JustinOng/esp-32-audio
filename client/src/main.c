#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#define AP_SSID "ESP32 Test"
#define AP_PASSWORD "network password"

EventGroupHandle_t wifi_event_group;

const int CONNECTED_BIT = BIT0;

static const char* TAG = "TEST";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
  switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
      break;
    default:
      break;
  }
  return ESP_OK;
}

static void initialise_wifi(void)
{
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

  // https://www.esp32.com/viewtopic.php?t=340
  tcpip_adapter_init();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
  wifi_config_t ap_cfg;
  ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  // docs says default 4 max 4 but without explicitly setting it, i keep getting max connections?
  ap_cfg.ap.max_connection = 4;
  // memcpy instead of strcpy because ap.ssid and ap.password are declared as uint8_t for some reason :(
  memcpy(ap_cfg.ap.ssid, AP_SSID, strlen(AP_SSID));
  memcpy(ap_cfg.ap.password, AP_PASSWORD, strlen(AP_PASSWORD));
  ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", ap_cfg.ap.ssid);

  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_cfg) );
  ESP_ERROR_CHECK( esp_wifi_start() );

  tcpip_adapter_ip_info_t ipInfo;
  IP4_ADDR(&ipInfo.ip, 192,168,1,1);
  IP4_ADDR(&ipInfo.gw, 192,168,1,1);
  IP4_ADDR(&ipInfo.netmask, 255,255,255,0);
  tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ipInfo);
}

static void wpa2_enterprise_example_task(void *pvParameters)
{
  tcpip_adapter_ip_info_t ip;
  memset(&ip, 0, sizeof(tcpip_adapter_ip_info_t));
  vTaskDelay(2000 / portTICK_PERIOD_MS);

  while (1) {
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP , &ip) == 0) {
      ESP_LOGI(TAG, "~~~~~~~~~~~");
      ESP_LOGI(TAG, "IP:"IPSTR, IP2STR(&ip.ip));
      ESP_LOGI(TAG, "MASK:"IPSTR, IP2STR(&ip.netmask));
      ESP_LOGI(TAG, "GW:"IPSTR, IP2STR(&ip.gw));
      ESP_LOGI(TAG, "~~~~~~~~~~~");
    }
  }
}

void app_main()
{
  ESP_ERROR_CHECK( nvs_flash_init() );
  initialise_wifi();
  xTaskCreate(&wpa2_enterprise_example_task, "wpa2_enterprise_example_task", 4096, NULL, 5, NULL);
}
