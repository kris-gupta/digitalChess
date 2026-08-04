#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ansi.h"
#include "board.h"
#include "chess.h"
#include "http.h"
#include "json.h"
#include "wifi.h"

static const char *TAG = "app_main";

// Maximum PGN body size for the broadcast push.
// A full 100-move game with headers fits comfortably inside 4 KB.
#define PGN_BUF_SIZE 4096

// Recolors ESP_LOG output by level (CONFIG_LOG_COLORS is off, so the format
// string here is plain and starts with the level letter: 'E'/'W'/'I'/...).
// Registered as the log backend in app_main() so it applies to every
// ESP_LOG call in the app, including from IDF components like wifi.
static int colored_vprintf(const char *fmt, va_list args) {
  char buf[512];
  int full_len = vsnprintf(buf, sizeof(buf), fmt, args);
  if (full_len <= 0)
    return full_len;

  int written = full_len < (int)sizeof(buf) ? full_len : (int)sizeof(buf) - 1;
  bool has_nl = (written > 0 && buf[written - 1] == '\n');
  if (has_nl)
    buf[written - 1] = '\0';

  const char *color;
  switch (buf[0]) {
  case 'E':
    color = ANSI_RED;
    break;
  case 'W':
    color = ANSI_YELLOW;
    break;
  case 'I':
    color = ANSI_BLUE;
    break;
  default:
    color = ANSI_CYAN;
    break;
  }

  fputs(color, stdout);
  fputs(buf, stdout);
  fputs(ANSI_RESET, stdout);
  if (has_nl)
    fputc('\n', stdout);

  return full_len;
}

// Installed as a constructor (rather than at the top of app_main) so it's
// live for as much of the boot log as application code can reach — ESP-IDF
// component init logs run before app_main is ever called.
__attribute__((constructor)) static void install_colored_log(void) {
  esp_log_set_vprintf(colored_vprintf);
}

static esp_err_t check_http_status(esp_http_client_handle_t clt) {
  int status = esp_http_client_get_status_code(clt);
  if (status >= 400) {
    ESP_LOGE(TAG, "HTTP %d", status);
    return ESP_FAIL;
  }
  return ESP_OK;
}

// Parses the first string field named `key` out of the JSON in
// response_buffer into out (null-terminated).
static esp_err_t parse_field(const char *response_buffer, const char *key,
                             char *out, size_t out_size) {
  struct json j = json_parse(response_buffer, strlen(response_buffer), 256);
  if (!j.tokens)
    return ESP_FAIL;

  int val_len = 0;
  const char *val = json_get_value(&j, response_buffer, key, &val_len);
  if (val && val_len > 0) {
    int copy = val_len < (int)out_size - 1 ? val_len : (int)out_size - 1;
    memcpy(out, val, copy);
    out[copy] = '\0';
  }
  json_free(&j);
  return out[0] ? ESP_OK : ESP_FAIL;
}

// Parses the first "id" string field from the JSON currently in response_buffer
// into out (null-terminated).
static esp_err_t parse_id(const char *response_buffer, char *out,
                          size_t out_size) {
  return parse_field(response_buffer, "id", out, out_size);
}

// Creates a Lichess broadcast tournament and its first round.
// Logs the spectator URL to the serial console.
// tour_id and round_id must be at least 32 bytes each.
static esp_err_t create_broadcast(esp_http_client_handle_t clt,
                                  char *response_buffer, char *tour_id,
                                  char *round_id) {
  esp_http_client_set_header(clt, "Authorization", BEARER_TOKEN);

  // 1. Create tournament
  esp_err_t err = http_post(clt, "https://lichess.org/broadcast/new",
                            "application/x-www-form-urlencoded",
                            "name=Physical+Chess&shortDescription=OTB+game");
  if (err != ESP_OK || check_http_status(clt) != ESP_OK)
    return ESP_FAIL;

  if (parse_id(response_buffer, tour_id, 32) != ESP_OK) {
    ESP_LOGE(TAG, "failed to parse broadcast tournament id");
    return ESP_FAIL;
  }
  memset(response_buffer, 0, MAX_HTTP_RECV_BUFFER);

  // 2. Create round inside the tournament
  char round_url[96];
  snprintf(round_url, sizeof(round_url),
           "https://lichess.org/broadcast/%s/new", tour_id);
  err = http_post(clt, round_url, "application/x-www-form-urlencoded",
                  "name=Game+1");
  if (err != ESP_OK || check_http_status(clt) != ESP_OK)
    return ESP_FAIL;

  if (parse_id(response_buffer, round_id, 32) != ESP_OK) {
    ESP_LOGE(TAG, "failed to parse broadcast round id");
    return ESP_FAIL;
  }

  // Lichess returns the round's real spectator URL directly in the response
  // (BroadcastRoundInfo.url) — use that instead of guessing at a slug format.
  char spectator_url[128] = {0};
  parse_field(response_buffer, "url", spectator_url, sizeof(spectator_url));
  memset(response_buffer, 0, MAX_HTTP_RECV_BUFFER);

  if (spectator_url[0])
    ESP_LOGI(TAG, "Broadcast: %s", spectator_url);
  else
    ESP_LOGW(TAG, "created broadcast round %s but could not parse its url",
             round_id);
  return ESP_OK;
}

// Pushes the current PGN to the broadcast round.
// Lichess accepts incremental pushes; each call replaces the previous state.
static esp_err_t push_pgn(esp_http_client_handle_t clt, const char *round_id,
                          const char *pgn) {
  char url[96];
  snprintf(url, sizeof(url), "https://lichess.org/api/broadcast/round/%s/push",
           round_id);
  esp_err_t err = http_post(clt, url, "text/plain", pgn);
  if (err != ESP_OK || check_http_status(clt) != ESP_OK) {
    ESP_LOGE(TAG, "pgn push failed");
    return ESP_FAIL;
  }
  return ESP_OK;
}

void app_main(void) {
  char *response_buffer = NULL;
  char *pgn_buf = NULL;
  esp_http_client_handle_t clt = NULL;

  // NVS flash init
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // WiFi
  ESP_LOGI(TAG, "connecting to WiFi");
  if (wifi_init_sta() != ESP_OK) {
    ESP_LOGE(TAG, "WiFi failed, aborting");
    return;
  }

  // SNTP — TLS certificate validation requires a valid system clock
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
  {
    time_t now = 0;
    struct tm timeinfo = {0};
    int sntp_retry = 0;
    while (timeinfo.tm_year < (2024 - 1900) && ++sntp_retry < 30) {
      ESP_LOGI(TAG, "waiting for NTP sync... (%d)", sntp_retry);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      time(&now);
      localtime_r(&now, &timeinfo);
    }
  }

  // HTTP client
  response_buffer = (char *)calloc(MAX_HTTP_RECV_BUFFER, 1);
  pgn_buf = (char *)calloc(PGN_BUF_SIZE, 1);
  if (!response_buffer || !pgn_buf) {
    ESP_LOGE(TAG, "failed to alloc buffers");
    goto cleanup;
  }

  esp_http_client_config_t cfg = {
      .host = HTTP_HOST,
      .path = HTTP_PATH,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .event_handler = http_event_handler,
      .user_data = response_buffer,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 30000,
  };
  clt = http_init(response_buffer, &cfg);

  // Create the broadcast — the spectator URL is logged to serial
  char tour_id[32] = {0};
  char round_id[32] = {0};
  struct Game game;
  board_init(&game);

  if (create_broadcast(clt, response_buffer, tour_id, round_id) != ESP_OK) {
    ESP_LOGE(TAG, "failed to create broadcast, aborting");
    goto cleanup;
  }

  // PGN_BUF_SIZE - 2 reserves the last two bytes for the trailing "* " + '\0'
  // that is written temporarily during each push.
  const int pgn_cap = PGN_BUF_SIZE - 2;

  // Seed PGN header; pushed to Lichess after each move
  int pgn_len = snprintf(pgn_buf, pgn_cap,
                         "[Event \"Physical Chess\"]\n"
                         "[White \"White\"]\n"
                         "[Black \"Black\"]\n"
                         "[Result \"*\"]\n\n");

  // Game loop
  int move_num = 1;
  bool skip_next = false;

  while (1) {
    struct Move m = scanb();

    if (skip_next) {
      // Rook physically moved after castling — board state was already updated
      // when the king moved; just consume this scanb() without submitting.
      skip_next = false;
      continue;
    }

    // Generate SAN *before* applying the move (piece is still at m.from)
    char san[9];
    move_to_san(&game, m, san);

    bool skip_next_out = false;
    board_apply_move(&game, m, &skip_next_out);
    skip_next = skip_next_out;

    // Append move to PGN (leave the last 2 bytes free for the '*' push below)
    if (pgn_len < pgn_cap) {
      if (game.current_turn == WHITE) {
        pgn_len += snprintf(pgn_buf + pgn_len, pgn_cap - pgn_len, "%d. %s ",
                            move_num, san);
      } else {
        pgn_len += snprintf(pgn_buf + pgn_len, pgn_cap - pgn_len, "%s ", san);
        move_num++;
      }
      if (pgn_len > pgn_cap)
        pgn_len = pgn_cap;
    }

    // Temporarily append '*' (ongoing game marker), push, then remove it.
    // Avoids a second 4 KB buffer on the stack.
    pgn_buf[pgn_len] = '*';
    pgn_buf[pgn_len + 1] = '\0';
    esp_err_t err = push_pgn(clt, round_id, pgn_buf);
    pgn_buf[pgn_len] = '\0';
    memset(response_buffer, 0, MAX_HTTP_RECV_BUFFER);

    ESP_LOGI(TAG, "[%s] %s %s", game.current_turn == WHITE ? "W" : "B", san,
             err == ESP_OK ? "ok" : "push failed");

    game.current_turn = (game.current_turn == WHITE) ? BLACK : WHITE;
  }

cleanup:
  if (clt)
    esp_http_client_cleanup(clt);
  free(response_buffer);
  free(pgn_buf);
}
