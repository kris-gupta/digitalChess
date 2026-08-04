#define JSMN_STATIC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "ansi.h"
#include "json.h"

struct json json_parse(const char *buf, int len, int max_tokens) {
  const char *TAG = "json_parse";
  struct json j = {NULL, 0};

  j.tokens = calloc(max_tokens, sizeof(jsmntok_t));
  if (j.tokens == NULL) {
    ESP_LOGE(TAG, "failed to allocate tokens");
    return j;
  }

  jsmn_parser parser;
  jsmn_init(&parser);

  int result = jsmn_parse(&parser, buf, len, j.tokens, max_tokens);
  if (result < 0) {
    ESP_LOGE(TAG, "parse error: %d", result);
    free(j.tokens);
    j.tokens = NULL;
    return j;
  }

  j.count = result;
  return j;
}

void json_free(struct json *j) {
  if (j->tokens) {
    free(j->tokens);
    j->tokens = NULL;
  }
  j->count = 0;
}

const char *json_get_value(const struct json *j, const char *buf,
                           const char *key, int *out_len) {
  for (int i = 0; i + 1 < j->count; i++) {
    if (j->tokens[i].type != JSMN_STRING)
      continue;
    int klen = j->tokens[i].end - j->tokens[i].start;
    if ((int)strlen(key) == klen &&
        memcmp(key, buf + j->tokens[i].start, klen) == 0) {
      int vlen = j->tokens[i + 1].end - j->tokens[i + 1].start;
      if (out_len)
        *out_len = vlen;
      return buf + j->tokens[i + 1].start;
    }
  }
  if (out_len)
    *out_len = 0;
  return NULL;
}

void json_print(struct json j, char *buffer, int max_len) {
  fputs(ANSI_CYAN, stdout);
  for (int i = 0; i < j.count; i++) {
    printf("%*s", j.tokens[i].end - j.tokens[i].start,
           buffer + j.tokens[i].start);
  }
  fputs(ANSI_RESET, stdout);
}
