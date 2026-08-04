#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "esp_tls.h"
#include "esp_tls_errors.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "http.h"

static inline int min_int(int a, int b) { return a < b ? a : b; }

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  const char *TAG = "http_event_handler";
  static char *output_buffer;
  static int output_len;
  switch (evt->event_id) {
  case HTTP_EVENT_ERROR:
    ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
    output_len = 0;
    break;
  case HTTP_EVENT_HEADER_SENT:
    ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
    break;
  case HTTP_EVENT_ON_HEADER:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key,
             evt->header_value);
    break;
  case HTTP_EVENT_ON_FINISH:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH. total collected: %d bytes",
             output_len);
    if (output_buffer != NULL) {
      ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
      free(output_buffer);
      output_buffer = NULL;
    }
    output_len = 0;
    break;
  case HTTP_EVENT_DISCONNECTED: {
    ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
    int mbedtls_err = 0;
    esp_err_t err = esp_tls_get_and_clear_last_error(
        (esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
    if (err != 0) {
      ESP_LOGE(TAG, "esp error code: 0x%x", err);
      ESP_LOGE(TAG, "mbedtls failure: 0x%x", mbedtls_err);
    }
    output_len = 0;
    break;
  }
  case HTTP_EVENT_REDIRECT:
    ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

    if (output_len == 0 && evt->user_data)
      memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);

    if (!esp_http_client_is_chunked_response(evt->client)) {
      int copy_len = 0;
      if (evt->user_data) {
        copy_len = min_int(evt->data_len, MAX_HTTP_OUTPUT_BUFFER - output_len);
        if (copy_len)
          memcpy(evt->user_data + output_len, evt->data, copy_len);
      } else {
        int content_len = esp_http_client_get_content_length(evt->client);
        if (output_buffer == NULL) {
          output_buffer = (char *)calloc(content_len + 1, sizeof(char));
          output_len = 0;
          if (output_buffer == NULL) {
            ESP_LOGE(TAG, "failed to allocate memory for output buffer");
            return ESP_FAIL;
          }
        }
        copy_len = min_int(evt->data_len, content_len - output_len);
        if (copy_len)
          memcpy(output_buffer + output_len, evt->data, copy_len);
      }
      output_len += copy_len;
    } else {
      if (!evt->user_data) {
        ESP_LOGE(TAG, "chunked response requires a user_data buffer");
        return ESP_FAIL;
      }
      char *buffer = (char *)evt->user_data;
      int copy_len = evt->data_len;
      if (output_len + copy_len > MAX_HTTP_OUTPUT_BUFFER - 1) {
        copy_len = (MAX_HTTP_OUTPUT_BUFFER - 1) - output_len;
        ESP_LOGW(TAG, "response too large, truncating data...");
      }
      if (copy_len > 0) {
        memcpy(buffer + output_len, evt->data, copy_len);
        output_len += copy_len;
        buffer[output_len] = '\0';
      }
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

esp_http_client_handle_t http_init(char *response_buffer,
                                   esp_http_client_config_t *cfg) {
  const char *TAG = "http_init";
  esp_http_client_handle_t clt = esp_http_client_init(cfg);
  ESP_LOGI(TAG, "configured http client");
  return clt;
}

void http_get(esp_http_client_handle_t clt, const char *url) {
  const char *TAG = "http_get";
  esp_http_client_set_url(clt, url);
  esp_http_client_set_method(clt, HTTP_METHOD_GET);
  esp_err_t err = esp_http_client_perform(clt);
  if (err == ESP_OK)
    ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
             esp_http_client_get_status_code(clt),
             esp_http_client_get_content_length(clt));
  else
    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
}

// Retries on transient connect/handshake failures (e.g. a dropped TLS
// ClientHello); a fresh esp_http_client_perform() call re-opens the
// underlying connection each time.
#define HTTP_POST_MAX_ATTEMPTS 3

esp_err_t http_post(esp_http_client_handle_t clt, const char *url,
                    const char *content_type, const char *body) {
  const char *TAG = "http_post";
  esp_http_client_set_url(clt, url);
  esp_http_client_set_method(clt, HTTP_METHOD_POST);
  if (content_type)
    esp_http_client_set_header(clt, "Content-Type", content_type);
  int body_len = body ? (int)strlen(body) : 0;
  esp_http_client_set_post_field(clt, body, body_len);

  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= HTTP_POST_MAX_ATTEMPTS; attempt++) {
    err = esp_http_client_perform(clt);
    if (err == ESP_OK)
      break;
    ESP_LOGW(TAG, "attempt %d/%d failed: %s", attempt, HTTP_POST_MAX_ATTEMPTS,
             esp_err_to_name(err));
    // esp_http_client keeps the underlying connection open across perform()
    // calls (HTTP keep-alive). If the server (or an idle-timeout proxy)
    // silently closed a stale connection between requests, perform() alone
    // just retries on the same dead socket and fails identically every time.
    // Force a fresh connection before retrying.
    esp_http_client_close(clt);
    if (attempt < HTTP_POST_MAX_ATTEMPTS)
      vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return err;
}
