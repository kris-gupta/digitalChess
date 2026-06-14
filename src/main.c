#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_log_level.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "chess.h"
#include "http.h"
#include "json.h"
#include "wifi.h"

static esp_err_t check_http_status(esp_http_client_handle_t clt) {
  int status = esp_http_client_get_status_code(clt);
  if (status >= 400) {
    ESP_LOGE("http", "HTTP %d response", status);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t create_seek(esp_http_client_handle_t clt) {
  esp_http_client_set_url(clt, "https://lichess.org/api/board/seek");
  esp_http_client_set_method(clt, HTTP_METHOD_POST);
  esp_http_client_set_post_field(
      clt, "rated=true&variant=standard&time=15&increment=15&color=white", -1);
  esp_http_client_set_header(clt, "Content-Type",
                             "application/x-www-form-urlencoded");
  esp_err_t err = esp_http_client_perform(clt);
  return err == ESP_OK ? check_http_status(clt) : err;
}

esp_err_t set_move(char *player_id, struct Move move,
                   esp_http_client_handle_t clt) {

  char url[128];
  char move_str[8];
  char move_from_hor = (move.from >> 4) + 'a';
  char move_from_ver = (move.from & 0x0F) + '1';
  char move_to_hor = (move.to >> 4) + 'a';
  char move_to_ver = (move.to & 0x0F) + '1';

  snprintf(move_str, sizeof(move_str), "%c%c%c%c", move_from_hor, move_from_ver,
           move_to_hor, move_to_ver);
  snprintf(url, sizeof(url),
           "https://lichess.org/api/board/game/%s/move/%s?offeringDraw=false",
           player_id, move_str);

  esp_http_client_set_url(clt, url);
  esp_http_client_set_method(clt, HTTP_METHOD_POST);
  esp_err_t err = esp_http_client_perform(clt);
  return err == ESP_OK ? check_http_status(clt) : err;
}

void app_main(void) {
  const char *TAG = "app_main";

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
  ESP_LOGI(TAG, "starting WiFi");
  if (wifi_init_sta() != ESP_OK) {
    ESP_LOGE(TAG, "WiFi connection failed, aborting");
    return;
  }

  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  time_t now = 0;
  struct tm timeinfo = {0};
  int retry = 0;
  while (timeinfo.tm_year < (2024 - 1900) && ++retry < 30) {
    ESP_LOGI(TAG, "waiting for NTP time sync... (%d)", retry);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    time(&now);
    localtime_r(&now, &timeinfo);
  }
  char strftime_buf[64];
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGI(TAG, "current time: %s", strftime_buf);

  char *response_buffer = (char *)calloc(MAX_HTTP_RECV_BUFFER, 1);

  esp_http_client_config_t cfg = {
      .host = HTTP_HOST,
      .path = HTTP_PATH,
      .query = HTTP_QUERY,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .event_handler = _http_event_handler,
      .user_data = response_buffer,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 0,
  };

  esp_http_client_handle_t clt = http_init(response_buffer, &cfg);
  esp_http_client_set_header(clt, "Authorization", BEARER_TOKEN);
  http_get(clt, response_buffer);

  struct Player player = {0};

  {
    struct json j = json_parse(response_buffer, strlen(response_buffer), 128);
    // json_print(j, response_buffer, strlen(response_buffer));

    if (j.tokens) {
      int id_len = 0;
      const char *id = json_get_value(&j, response_buffer, "id", &id_len);
      if (id && id_len > 0) {
        int copy = id_len < (int)sizeof(player.player_id) - 1
                       ? id_len
                       : (int)sizeof(player.player_id) - 1;
        memcpy(player.player_id, id, copy);
        player.player_id[copy] = '\0';
      }
      json_free(&j);
    }
    memset(response_buffer, 0, strlen(response_buffer));
  }

  {
    esp_err_t err = create_seek(clt);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "create_seek failed: %s", esp_err_to_name(err));
    } else {
      struct json j = json_parse(response_buffer, strlen(response_buffer), 128);
      // json_print(j, response_buffer, strlen(response_buffer));
      memset(response_buffer, 0, strlen(response_buffer));
      json_free(&j);
    }
  }

  {
    struct Move move = {.from = 68, .to = 69};
    esp_err_t err = set_move(player.player_id, move, clt);
    ESP_LOGI(TAG, "set_move returned: %s", esp_err_to_name(err));
    struct json j = json_parse(response_buffer, strlen(response_buffer), 128);
    // json_print(j, response_buffer, strlen(response_buffer));
    memset(response_buffer, 0, strlen(response_buffer));
    json_free(&j);
  }

  esp_http_client_cleanup(clt);
  free(response_buffer);
}
