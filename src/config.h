#pragma once

#define CONFIG_PATH     "ms0:/PSP/GAME/MopiTube/config.txt"
#define MAX_HOST_LEN    64
#define MAX_PASS_LEN    64

typedef struct {
    char host[MAX_HOST_LEN];
    int  port;
    char password[MAX_PASS_LEN];
    int  wifi_profile;
    int  http_port;     /* Mopidy HTTP API port for art fetch (default 6680) */
} Config;

extern Config g_config;

/* Return codes for config_load(). */
#define CONFIG_OK           0
#define CONFIG_ERR_MISSING  (-1)   /* file not found */
#define CONFIG_ERR_INVALID  (-2)   /* value out of range */

int config_load(void);
