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
#include <lwip/sockets.h>

#define AP_SSID "ESP32 Test"
#define AP_PASSWORD "network password"

#define PORT_NUMBER 8001

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
  tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
  tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ipInfo);
  tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);
}

static void listen_udp(void *pvParameters)
{
  struct sockaddr_in clientAddress;
	struct sockaddr_in serverAddress;

	// Create a socket that we will listen upon.
	int sock = socket(AF_INET, SOCK_DGRAM , IPPROTO_UDP );
	if (sock < 0) {
		ESP_LOGE(TAG, "socket: %d %s", sock, strerror(errno));
		goto END;
	}

	// Bind our server socket to a port.
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(PORT_NUMBER);
	int rc  = bind(sock, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
	if (rc < 0) {
		ESP_LOGE(TAG, "bind: %d %s", rc, strerror(errno));
		goto END;
	}
  /*
	// Flag the socket as listening for new connections.
	rc = listen(sock, 5);
	if (rc < 0) {
		ESP_LOGE(TAG, "listen: %d %s", rc, strerror(errno));
		goto END;
	}*/

	while (1) {
		char *buffer = malloc(1032);

    struct sockaddr_storage src_addr;
    socklen_t src_addr_len = sizeof(src_addr);
    ssize_t count = recvfrom(sock,buffer,sizeof(buffer),0,(struct sockaddr*)&src_addr,&src_addr_len);
    if (count == -1) {
      ESP_LOGE(TAG, "recvfrom: %s",strerror(errno));
      goto END;
    }

		// http://stackoverflow.com/a/8170756
		ESP_LOGD(TAG, "Data read (size: %d) was: %.*s", count, count, buffer);
		free(buffer);
	}
	END:
	vTaskDelete(NULL);
}

void app_main()
{
  ESP_ERROR_CHECK( nvs_flash_init() );
  initialise_wifi();
  xTaskCreate(&listen_udp, "listen_udp", 4096, NULL, 5, NULL);
}
