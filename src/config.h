#ifndef CONFIG_H
#define CONFIG_H

#include "zabbix_api.h"

#define MAX_WIDGETS 64
#define MAX_NAME 256

/* Widget types */
typedef enum {
    WIDGET_GAUGE = 0,
    WIDGET_CARD = 1,
    WIDGET_TREND = 2
} WidgetType;

/* Widget configuration */
typedef struct {
    WidgetType type;
    char item_id[32];
    char host_name[MAX_NAME];
    char item_name[MAX_NAME];
    char units[64];
    int  value_type;    /* Zabbix value type */

    /* Window position & size */
    int x, y;
    int width, height;

    /* Behavior */
    int always_on_top;
    int refresh_interval;  /* seconds */

    /* Gauge specific */
    double gauge_min;
    double gauge_max;
    double gauge_warn;     /* warning threshold */
    double gauge_crit;     /* critical threshold */
    int    gauge_warn_enabled;
    int    gauge_crit_enabled;

    /* Trend specific */
    int trend_hours;       /* hours of history to show */

    /* Visual */
    int bg_opacity;       /* 0-255, background alpha */
    int accent_color;     /* RGB (0xBBGGRR format) */
} WidgetConfig;

/* Application configuration */
typedef struct {
    /* Zabbix connection */
    char zabbix_url[512];
    char zabbix_user[128];
    char zabbix_pass[128];

    /* Widgets */
    WidgetConfig widgets[MAX_WIDGETS];
    int widget_count;
} AppConfig;

/* Initialize default config */
void config_init(AppConfig *cfg);

/* Load config from file. Returns 0 on success. */
int config_load(AppConfig *cfg, const char *filepath);

/* Save config to file. Returns 0 on success. */
int config_save(const AppConfig *cfg, const char *filepath);

/* Get default config file path (in %APPDATA%\ZabbixDesktopInfo\config.json) */
void config_get_default_path(char *path, int path_size);

/* Add a widget config. Returns index, -1 if full. */
int config_add_widget(AppConfig *cfg, const WidgetConfig *wc);

/* Remove a widget by index */
void config_remove_widget(AppConfig *cfg, int index);

#endif /* CONFIG_H */
