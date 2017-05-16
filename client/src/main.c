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
#include "driver/i2s.h"

// for audio isr
#define TIMER_DIVIDER 1
#define TRK_TIMER_GROUP	TIMER_GROUP_0
#define TRK_TIMER_IDX	TIMER_1
// technically is 1814.05, what do i do with the .05? track and increment when it exceeds 1 count?
#define AUDIO_ISR_INTERVAL 1814

#define PORT_NUMBER 8001

#define RX_BUFFER_SIZE 1032

#define I2S_NUM 0

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

static void initialise_i2s() {
  static const i2s_config_t i2s_config = {
     .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
     .sample_rate = 44100,
     .bits_per_sample = 16, /* the DAC module will only take the 8bits from MSB */
     .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
     .communication_format = I2S_COMM_FORMAT_I2S_MSB,
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
     .dma_buf_count = 8,
     .dma_buf_len = 64
  };

  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, NULL);
  i2s_set_sample_rates(I2S_NUM, 44100); //set sample rates
}

static void listen_audio_data(void *pvParameters)
{
	struct sockaddr_in serverAddress;

	// Create a socket that we will listen upon.
	int rx_sock = socket(AF_INET, SOCK_DGRAM , 0);
	if (rx_sock < 0) {
		ESP_LOGE(TAG, "rx socket: %d %s", rx_sock, strerror(errno));
		goto END;
	}

	// Bind our server socket to a port.
  memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(PORT_NUMBER);
	int rc  = bind(rx_sock, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
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

  int tx_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (tx_sock < 0) {
		ESP_LOGE(TAG, "tx socket: %d %s", tx_sock, strerror(errno));
		goto END;
	}

  uint32_t last_packet_index = 0;

	while (1) {
		char *buffer = malloc(RX_BUFFER_SIZE);
    uint32_t packet_index = 0;

    struct sockaddr_storage src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    ssize_t count = recvfrom(rx_sock,buffer,RX_BUFFER_SIZE,0,(struct sockaddr*)&src_addr,&src_addr_len);
    if (count == -1) {
      ESP_LOGE(TAG, "recvfrom: %s",strerror(errno))
      goto END;
    }
		// http://stackoverflow.com/a/8170756
		ESP_LOGI(TAG, "Data read (size: %d)", count);/*
    for(uint8_t i = 0; i < 255; i++) {
      ESP_LOGI(TAG, "Byte %d: %02x", i, buffer[i]);
    }*/

    for(uint8_t i = 0; i < 4; i++) {
      packet_index |= buffer[i] << (8 * i);
    }

    if (packet_index > last_packet_index) {
      // skip first 4 bytes because they represent the sequence number of the packet
      i2s_write_bytes(I2S_NUM, buffer+(4*sizeof(buffer)), count-4, portMAX_DELAY);

      last_packet_index = packet_index;
    }

    sendto(rx_sock, &last_packet_index, 4, 0, (struct sockaddr *)&src_addr, src_addr_len);
		free(buffer);
	}
	END:
	vTaskDelete(NULL);
}

void app_main()
{
  ESP_ERROR_CHECK( nvs_flash_init() );
  initialise_wifi();
  initialise_i2s();
  xTaskCreate(&listen_audio_data, "listen_audio_data", 4096, NULL, 5, NULL);
}
