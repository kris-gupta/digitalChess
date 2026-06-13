#ifndef JSON_H
#define JSON_H

#include "jsmn.h"

struct json {
    jsmntok_t *tokens;
    int count;
};

struct json json_parse(const char *buf, int len, int max_tokens);
void json_free(struct json *j);
const char *json_get_value(const struct json *j, const char *buf,
                           const char *key, int *out_len);

void json_print(struct json j, char *buffer, int max_len);
#endif
