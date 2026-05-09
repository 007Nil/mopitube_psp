#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "config.h"

Config g_config = {
    .host         = "192.168.1.100",
    .port         = 6600,
    .password     = "",
    .wifi_profile = 1,
    .http_port    = 6680,
};

/* Trim leading/trailing whitespace in-place. */
static void trim(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

int config_load(void) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return CONFIG_ERR_MISSING;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *cr = strchr(line, '\r'); if (cr) *cr = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "host") == 0) {
            strncpy(g_config.host, val, MAX_HOST_LEN - 1);
            g_config.host[MAX_HOST_LEN - 1] = '\0';
        } else if (strcmp(key, "port") == 0) {
            g_config.port = atoi(val);
        } else if (strcmp(key, "password") == 0) {
            strncpy(g_config.password, val, MAX_PASS_LEN - 1);
            g_config.password[MAX_PASS_LEN - 1] = '\0';
            /* Strip control characters that would corrupt the MPD command. */
            for (char *p = g_config.password; *p; p++) {
                if ((unsigned char)*p < 0x20 || *p == 0x7F) { *p = '\0'; break; }
            }
        } else if (strcmp(key, "wifi_profile") == 0) {
            g_config.wifi_profile = atoi(val);
        } else if (strcmp(key, "http_port") == 0) {
            g_config.http_port = atoi(val);
        }
    }

    fclose(f);

    if (g_config.host[0] == '\0')                            return CONFIG_ERR_INVALID;
    if (g_config.port < 1 || g_config.port > 65535)         return CONFIG_ERR_INVALID;
    if (g_config.wifi_profile < 1 || g_config.wifi_profile > 9) return CONFIG_ERR_INVALID;
    if (g_config.http_port < 1 || g_config.http_port > 65535) return CONFIG_ERR_INVALID;

    return CONFIG_OK;
}
