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
#include "timer.h"
#include <lwip/sockets.h>

#define PORT_NUMBER 8080

// for audio isr
#define TIMER_DIVIDER 1
#define TRK_TIMER_GROUP	TIMER_GROUP_0
#define TRK_TIMER_IDX	TIMER_1
// technically is 1814.05, what do i do with the .05? track and increment when it exceeds 1 count?
#define AUDIO_ISR_INTERVAL 1814

EventGroupHandle_t wifi_event_group;

const int CONNECTED_BIT = BIT0;

static const char* TAG = "TEST";

const char* AP_SSID = "ESP32 Test";
const char* AP_PASSWORD = "hello123";

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
  // strlen() + 1 because strlen does not copy the null terminating character
  memcpy(ap_cfg.ap.ssid, AP_SSID, strlen(AP_SSID)+1);
  memcpy(ap_cfg.ap.password, AP_PASSWORD, strlen(AP_PASSWORD)+1);
  ESP_LOGI(TAG, "Setting WiFi configuration %s/%s...", ap_cfg.ap.ssid, ap_cfg.ap.password);

  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_cfg) );
  ESP_ERROR_CHECK( esp_wifi_start() );

  tcpip_adapter_ip_info_t ipInfo;
  IP4_ADDR(&ipInfo.ip, 192,168,1,1);
  IP4_ADDR(&ipInfo.gw, 192,168,1,1);
  IP4_ADDR(&ipInfo.netmask, 255,255,255,0);
  ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
  ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ipInfo));
  ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
}

void IRAM_ATTR timerIsr(void *para)
{
  static int i=0;
  ESP_LOGI(TAG, "ISR %d", i++);

  // clear alarm?
  TIMERG0.int_clr_timers.t1 = 1;
  TIMERG0.hw_timer[TRK_TIMER_IDX].config.alarm_en = 1;
}

static void initialise_audio_isr(void) {
  // https://www.esp32.com/viewtopic.php?t=1094
  timer_config_t config;
  config.alarm_en = 1;
  config.auto_reload = 1;
  config.counter_dir = TIMER_COUNT_UP;
  config.divider = TIMER_DIVIDER;
  config.intr_type = TIMER_INTR_LEVEL;
  config.counter_en = TIMER_PAUSE;

  timer_init(TRK_TIMER_GROUP, TRK_TIMER_IDX, &config);
  timer_set_counter_value(TRK_TIMER_GROUP, TRK_TIMER_IDX, 0x00000000ULL);
  timer_enable_intr(TRK_TIMER_GROUP, TRK_TIMER_IDX);
  timer_isr_register(TRK_TIMER_GROUP, TRK_TIMER_IDX, timerIsr, NULL, ESP_INTR_FLAG_IRAM, NULL);

  timer_pause(TRK_TIMER_GROUP, TRK_TIMER_IDX);
  timer_set_counter_value(TRK_TIMER_GROUP, TRK_TIMER_IDX, 0x00000000ULL);
  timer_set_alarm_value(TRK_TIMER_GROUP, TRK_TIMER_IDX, AUDIO_ISR_INTERVAL);
  timer_start(TRK_TIMER_GROUP, TRK_TIMER_IDX);
}

static void listen_audio_data(void *pvParameters)
{
  // https://github.com/nkolban/esp32-snippets/blob/master/sockets/server/socket_server.c
  struct sockaddr_in clientAddress;
  struct sockaddr_in serverAddress;

  // http://www.microhowto.info/howto/listen_for_and_receive_udp_datagrams_in_c.html
  // Create a socket that we will listen upon.
  // AF_INET: ipv4
  // SOCK_DGRAM & IPPROTO_UDP: self explanatory
  // use SOCK_STREAM, IPPROTO_TCP for tcp
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket: %d %s", sock, strerror(errno));
    return;
  }

  // Bind our server socket to a port.
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddress.sin_port = htons(PORT_NUMBER);
  int rc  = bind(sock, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
  if (rc < 0) {
    ESP_LOGE(TAG, "bind: %d %s", rc, strerror(errno));
    return;
  }

  // Flag the socket as listening for new connections.
  rc = listen(sock, 5);
  if (rc < 0) {
    ESP_LOGE(TAG, "listen: %d %s", rc, strerror(errno));
    return;
  }

  while (1) {

    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void app_main()
{
  ESP_ERROR_CHECK( nvs_flash_init() );
  initialise_wifi();
  xTaskCreate(&listen_audio_data, "listen_audio_data", 4096, NULL, 5, NULL);
}
