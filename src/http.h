#ifndef HTTP_H
#define HTTP_H

#include "esp_http_client.h"
#include "secrets.h"

#define HTTP_HOST "lichess.org"
#define HTTP_PATH "/api/account"
#define HTTP_QUERY ""
#define BEARER_TOKEN "Bearer " LICHESS_TOKEN
#define MAX_HTTP_RECV_BUFFER 4096
#define MAX_HTTP_OUTPUT_BUFFER 4096

esp_err_t _http_event_handler(esp_http_client_event_t *evt);
esp_http_client_handle_t http_init(char *response_buffer,
                                   esp_http_client_config_t *cfg);
void http_get(esp_http_client_handle_t clt, char *response_buffer);

#endif
