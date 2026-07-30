#ifndef ZABBIX_API_H
#define ZABBIX_API_H

#include "json.h"

/* Zabbix host info */
typedef struct {
    char id[32];
    char name[256];
    char host[256];
} ZabbixHost;

/* Zabbix item info */
typedef struct {
    char id[32];
    char name[256];
    char key[256];
    char units[64];
    char lastvalue[128];
    int  value_type;   /* 0=float,1=str,2=log,3=int,4=text */
} ZabbixItem;

/* History data point */
typedef struct {
    long long clock;
    double value;
} ZabbixHistoryPoint;

/* API context */
typedef struct {
    char url[512];        /* full API URL, e.g. http://host/zabbix/api_jsonrpc.php */
    char username[128];
    char password[128];
    char auth_token[256]; /* session token from user.login */
    int  connected;
} ZabbixAPI;

/* Initialize API context */
void zabbix_api_init(ZabbixAPI *api);

/* Login to Zabbix. Returns 0 on success. */
int zabbix_api_login(ZabbixAPI *api, const char *url, const char *user, const char *pass);

/* Get list of hosts. Returns count, -1 on error. Caller frees *hosts. */
int zabbix_api_get_hosts(ZabbixAPI *api, ZabbixHost **hosts);

/* Get items for a host. Returns count, -1 on error. Caller frees *items. */
int zabbix_api_get_items(ZabbixAPI *api, const char *host_id, ZabbixItem **items);

/* Get history for an item. Returns count, -1 on error. Caller frees *points. */
int zabbix_api_get_history(ZabbixAPI *api, const char *item_id, int value_type,
                           int time_from, int time_till,
                           ZabbixHistoryPoint **points);

/* Get last value for an item. Returns 0 on success, fills lastvalue. */
int zabbix_api_get_last_value(ZabbixAPI *api, const char *item_id,
                              char *lastvalue, int lastvalue_size);

/* Get item info (name, units, value_type) by itemid */
int zabbix_api_get_item_info(ZabbixAPI *api, const char *item_id, ZabbixItem *info);

/* Last error message */
const char *zabbix_api_error(void);

#endif /* ZABBIX_API_H */
