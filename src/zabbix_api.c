#include "zabbix_api.h"
#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#pragma comment(lib, "wininet.lib")

static char g_error[512] = {0};

/* ============ DEBUG LOG ============ */
/* Writes debug info to %TEMP%\zabbix_debug.log for troubleshooting */
static void debug_log(const char *fmt, ...)
{
    char path[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, path)) return;
    strncat(path, "zabbix_debug.log", sizeof(path) - strlen(path) - 2);

    FILE *f = fopen(path, "a");
    if (!f) return;

    /* Timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);
    fprintf(f, "[%s] ", ts);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);

    fprintf(f, "\n");
    fclose(f);
}

const char *zabbix_api_error(void)
{
    return g_error;
}

static void set_error(const char *msg)
{
    strncpy(g_error, msg, sizeof(g_error) - 1);
    g_error[sizeof(g_error) - 1] = '\0';
}

void zabbix_api_init(ZabbixAPI *api)
{
    memset(api, 0, sizeof(ZabbixAPI));
    InitializeCriticalSection(&api->request_cs);
    api->active_request = NULL;
}

/* Parse URL into scheme, host, port, path */
typedef struct {
    char scheme[16];  /* "http" or "https" */
    char host[256];
    int  port;
    char path[512];
} UrlParts;

static int parse_url(const char *url, UrlParts *u)
{
    memset(u, 0, sizeof(UrlParts));
    const char *p = url;

    if (_strnicmp(p, "https://", 8) == 0) {
        strcpy(u->scheme, "https");
        u->port = 443;
        p += 8;
    } else if (_strnicmp(p, "http://", 7) == 0) {
        strcpy(u->scheme, "http");
        u->port = 80;
        p += 7;
    } else {
        strcpy(u->scheme, "http");
        u->port = 80;
    }

    /* host[:port] */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    if (colon && (!slash || colon < slash)) {
        int hlen = (int)(colon - p);
        if (hlen >= (int)sizeof(u->host)) hlen = sizeof(u->host) - 1;
        memcpy(u->host, p, hlen);
        u->host[hlen] = '\0';
        u->port = atoi(colon + 1);
    } else {
        const char *end = slash ? slash : (p + strlen(p));
        int hlen = (int)(end - p);
        if (hlen >= (int)sizeof(u->host)) hlen = sizeof(u->host) - 1;
        memcpy(u->host, p, hlen);
        u->host[hlen] = '\0';
    }

    /* path */
    if (slash) {
        strncpy(u->path, slash, sizeof(u->path) - 1);
    } else {
        strcpy(u->path, "/");
    }

    /* If path doesn't end with api_jsonrpc.php, append it */
    if (strstr(u->path, "api_jsonrpc.php") == NULL) {
        size_t plen = strlen(u->path);
        if (plen > 0 && u->path[plen - 1] != '/')
            strncat(u->path, "/", sizeof(u->path) - plen - 1);
        strncat(u->path, "api_jsonrpc.php", sizeof(u->path) - strlen(u->path) - 1);
    }

    return 0;
}

/* Close the request handle exactly once, even if zabbix_api_abort raced us. */
static void close_request(ZabbixAPI *api, HINTERNET hRequest)
{
    int do_close = 0;
    EnterCriticalSection(&api->request_cs);
    if (api->active_request == hRequest) {
        api->active_request = NULL;
        do_close = 1;
    }
    LeaveCriticalSection(&api->request_cs);
    if (do_close) InternetCloseHandle(hRequest);
}

/* Perform HTTP POST and return response body (caller frees) */
static char *http_post(ZabbixAPI *api, const char *url, const char *body)
{
    UrlParts u;
    parse_url(url, &u);

    debug_log("HTTP POST to %s (host=%s port=%d path=%s body_len=%d)",
               url, u.host, u.port, u.path, (int)strlen(body));

    HINTERNET hSession = InternetOpenA("ZabbixDesktopInfo/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) {
        set_error("InternetOpen failed");
        debug_log("  InternetOpen FAILED: err=%lu", GetLastError());
        return NULL;
    }

    /* Set timeouts: 10s connect, 10s send, 10s receive */
    DWORD timeout = 10000;
    InternetSetOptionA(hSession, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hSession, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    int is_https = (_stricmp(u.scheme, "https") == 0);

    DWORD flags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
    if (is_https)
        flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
                 INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;

    HINTERNET hConnect = InternetConnectA(hSession, u.host,
        is_https ? (u.port == 443 ? INTERNET_DEFAULT_HTTPS_PORT : u.port) : u.port,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        set_error("InternetConnect failed");
        debug_log("  InternetConnect FAILED: err=%lu", GetLastError());
        InternetCloseHandle(hSession);
        return NULL;
    }

    HINTERNET hRequest = HttpOpenRequestA(hConnect, "POST", u.path,
        NULL, NULL, NULL, flags, 0);
    if (!hRequest) {
        set_error("HttpOpenRequest failed");
        debug_log("  HttpOpenRequest FAILED: err=%lu", GetLastError());
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return NULL;
    }

    /* Register the active request so it can be interrupted (see zabbix_api_abort) */
    EnterCriticalSection(&api->request_cs);
    api->active_request = hRequest;
    LeaveCriticalSection(&api->request_cs);

    /* For HTTPS: ignore all certificate errors (self-signed, unknown CA, etc.) */
    if (is_https) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        InternetSetOptionA(hRequest, INTERNET_OPTION_SECURITY_FLAGS,
                           &secFlags, sizeof(secFlags));
    }

    /* Use application/json for broader Zabbix version compatibility */
    const char *headers = "Content-Type: application/json-rpc\r\n";

    BOOL ok = HttpSendRequestA(hRequest, headers, (DWORD)strlen(headers),
        (LPVOID)body, (DWORD)strlen(body));
    if (!ok) {
        DWORD err = GetLastError();
        snprintf(g_error, sizeof(g_error), "HttpSendRequest failed (err=%lu)", err);
        debug_log("  HttpSendRequest FAILED: err=%lu", err);
        close_request(api, hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return NULL;
    }

    /* Read response */
    size_t total_size = 0;
    size_t buf_cap = 8192;
    char *response = (char *)malloc(buf_cap);
    if (!response) {
        close_request(api, hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return NULL;
    }

    DWORD bytes_read;
    char temp[4096];
    while (InternetReadFile(hRequest, temp, sizeof(temp), &bytes_read) && bytes_read > 0) {
        if (total_size + bytes_read >= buf_cap) {
            buf_cap = (total_size + bytes_read) * 2;
            response = (char *)realloc(response, buf_cap);
        }
        memcpy(response + total_size, temp, bytes_read);
        total_size += bytes_read;
    }
    response[total_size] = '\0';

    close_request(api, hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);

    debug_log("  Response: %d bytes: %.500s", (int)total_size, response);

    return response;
}

/* Build and send a JSON-RPC request. Returns the result JsonValue (caller frees). */
static JsonValue *api_call(ZabbixAPI *api, const char *method, JsonValue *params, int use_auth)
{
    static volatile LONG rpc_id = 0;
    int id = (int)InterlockedIncrement(&rpc_id);

    JsonValue *req = json_new_object();
    json_object_set(req, "jsonrpc", json_new_string("2.0"));
    json_object_set(req, "method", json_new_string(method));
    json_object_set(req, "params", params);
    if (use_auth && api->auth_token[0])
        json_object_set(req, "auth", json_new_string(api->auth_token));
    json_object_set(req, "id", json_new_number((double)id));

    char *body = json_stringify(req, 0);
    json_free(req);

    debug_log(">>> %s (id=%d, auth=%s): %s", method, id,
              (use_auth && api->auth_token[0]) ? "yes" : "no", body);

    char *resp_text = http_post(api, api->url, body);
    free(body);

    if (!resp_text) {
        debug_log("  http_post returned NULL: %s", g_error);
        return NULL;
    }

    JsonValue *resp = json_parse(resp_text);
    if (!resp) {
        debug_log("  Failed to parse response: %.200s", resp_text);
        set_error("Failed to parse API response");
        free(resp_text);
        return NULL;
    }
    free(resp_text);

    JsonValue *error = json_object_get(resp, "error");
    if (error) {
        const char *msg = json_string(json_object_get(error, "message"));
        const char *data = json_string(json_object_get(error, "data"));
        snprintf(g_error, sizeof(g_error), "API error: %s - %s",
                 msg ? msg : "?", data ? data : "?");
        debug_log("  API ERROR: %s - %s", msg ? msg : "?", data ? data : "?");
        json_free(resp);
        return NULL;
    }

    JsonValue *result = json_object_get(resp, "result");
    if (!result) {
        set_error("No 'result' in API response");
        debug_log("  No 'result' field in response");
        json_free(resp);
        return NULL;
    }

    /* Detach result from resp so caller can free resp */
    JsonValue *detached = (JsonValue *)calloc(1, sizeof(JsonValue));
    *detached = *result;
    /* Mark result as consumed so json_free(resp) doesn't free its contents */
    result->type = JSON_NULL;
    json_free(resp);

    debug_log("  OK: result type=%d", detached->type);
    return detached;
}

int zabbix_api_login(ZabbixAPI *api, const char *url, const char *user, const char *pass)
{
    strncpy(api->url, url, sizeof(api->url) - 1);
    strncpy(api->username, user, sizeof(api->username) - 1);
    strncpy(api->password, pass, sizeof(api->password) - 1);

    /* Zabbix 5.4+ uses "username", older uses "user" */
    JsonValue *params = json_new_object();
    json_object_set(params, "username", json_new_string(user));
    json_object_set(params, "password", json_new_string(pass));

    JsonValue *result = api_call(api, "user.login", params, 0);
    if (!result) {
        /* Try older API with "user" field */
        params = json_new_object();
        json_object_set(params, "user", json_new_string(user));
        json_object_set(params, "password", json_new_string(pass));
        result = api_call(api, "user.login", params, 0);
        if (!result) return -1;
    }

    const char *token = json_string(result);
    if (!token) {
        /* In some versions, result might be an object */
        set_error("Login: unexpected response format");
        json_free(result);
        return -1;
    }

    strncpy(api->auth_token, token, sizeof(api->auth_token) - 1);
    api->connected = 1;
    json_free(result);
    return 0;
}

int zabbix_api_get_hosts(ZabbixAPI *api, ZabbixHost **hosts)
{
    *hosts = NULL;

    JsonValue *params = json_new_object();

    JsonValue *output_arr = json_new_array();
    json_array_push(output_arr, json_new_string("hostid"));
    json_array_push(output_arr, json_new_string("host"));
    json_array_push(output_arr, json_new_string("name"));
    json_object_set(params, "output", output_arr);

    JsonValue *result = api_call(api, "host.get", params, 1);
    if (!result || result->type != JSON_ARRAY) {
        if (result) json_free(result);
        return -1;
    }

    int count = result->array.count;
    *hosts = (ZabbixHost *)calloc(count, sizeof(ZabbixHost));
    for (int i = 0; i < count; i++) {
        JsonValue *h = &result->array.items[i];
        const char *id   = json_string(json_object_get(h, "hostid"));
        const char *name = json_string(json_object_get(h, "name"));
        const char *host = json_string(json_object_get(h, "host"));
        if (id)   strncpy((*hosts)[i].id, id, sizeof((*hosts)[i].id) - 1);
        if (name) strncpy((*hosts)[i].name, name, sizeof((*hosts)[i].name) - 1);
        if (host) strncpy((*hosts)[i].host, host, sizeof((*hosts)[i].host) - 1);
    }

    json_free(result);
    return count;
}

int zabbix_api_get_items(ZabbixAPI *api, const char *host_id, ZabbixItem **items)
{
    *items = NULL;

    JsonValue *params = json_new_object();

    /* hostids as array for compatibility */
    JsonValue *host_arr = json_new_array();
    json_array_push(host_arr, json_new_string(host_id));
    json_object_set(params, "hostids", host_arr);

    JsonValue *output_arr = json_new_array();
    json_array_push(output_arr, json_new_string("itemid"));
    json_array_push(output_arr, json_new_string("name"));
    json_array_push(output_arr, json_new_string("key_"));
    json_array_push(output_arr, json_new_string("units"));
    json_array_push(output_arr, json_new_string("lastvalue"));
    json_array_push(output_arr, json_new_string("value_type"));
    json_object_set(params, "output", output_arr);

    /* Sort by name */
    json_object_set(params, "sortfield", json_new_string("name"));

    JsonValue *result = api_call(api, "item.get", params, 1);
    if (!result || result->type != JSON_ARRAY) {
        if (result) json_free(result);
        return -1;
    }

    int count = result->array.count;
    *items = (ZabbixItem *)calloc(count, sizeof(ZabbixItem));
    for (int i = 0; i < count; i++) {
        JsonValue *it = &result->array.items[i];
        const char *id   = json_string(json_object_get(it, "itemid"));
        const char *name = json_string(json_object_get(it, "name"));
        const char *key  = json_string(json_object_get(it, "key_"));
        const char *units = json_string(json_object_get(it, "units"));
        const char *lv   = json_string(json_object_get(it, "lastvalue"));
        JsonValue *vt    = json_object_get(it, "value_type");
        if (id)    strncpy((*items)[i].id, id, sizeof((*items)[i].id) - 1);
        if (name)  strncpy((*items)[i].name, name, sizeof((*items)[i].name) - 1);
        if (key)   strncpy((*items)[i].key, key, sizeof((*items)[i].key) - 1);
        if (units) strncpy((*items)[i].units, units, sizeof((*items)[i].units) - 1);
        if (lv)    strncpy((*items)[i].lastvalue, lv, sizeof((*items)[i].lastvalue) - 1);
        (*items)[i].value_type = (int)json_number(vt);
    }

    json_free(result);
    return count;
}

int zabbix_api_get_history(ZabbixAPI *api, const char *item_id, int value_type,
                           int time_from, int time_till,
                           ZabbixHistoryPoint **points)
{
    *points = NULL;

    JsonValue *params = json_new_object();
    json_object_set(params, "output", json_new_string("extend"));
    json_object_set(params, "history", json_new_number((double)value_type));

    /* itemids as array for compatibility */
    JsonValue *item_arr = json_new_array();
    json_array_push(item_arr, json_new_string(item_id));
    json_object_set(params, "itemids", item_arr);

    json_object_set(params, "sortfield", json_new_string("clock"));
    json_object_set(params, "sortorder", json_new_string("ASC"));
    json_object_set(params, "time_from", json_new_number((double)time_from));
    if (time_till > 0)
        json_object_set(params, "time_till", json_new_number((double)time_till));

    JsonValue *result = api_call(api, "history.get", params, 1);
    if (!result || result->type != JSON_ARRAY) {
        if (result) json_free(result);
        return -1;
    }

    int count = result->array.count;
    *points = (ZabbixHistoryPoint *)calloc(count > 0 ? count : 1, sizeof(ZabbixHistoryPoint));
    for (int i = 0; i < count; i++) {
        JsonValue *pt = &result->array.items[i];
        const char *clock = json_string(json_object_get(pt, "clock"));
        const char *val   = json_string(json_object_get(pt, "value"));
        (*points)[i].clock = clock ? atoll(clock) : 0;
        (*points)[i].value = val ? atof(val) : 0.0;
    }

    json_free(result);
    return count;
}

int zabbix_api_get_last_value(ZabbixAPI *api, const char *item_id,
                              char *lastvalue, int lastvalue_size)
{
    debug_log("zabbix_api_get_last_value: item_id='%s', connected=%d",
              item_id ? item_id : "(null)", api->connected);

    if (!item_id || !item_id[0]) {
        debug_log("  item_id is empty!");
        set_error("item_id is empty");
        return -1;
    }

    JsonValue *params = json_new_object();

    /* itemids as array for compatibility */
    JsonValue *item_arr = json_new_array();
    json_array_push(item_arr, json_new_string(item_id));
    json_object_set(params, "itemids", item_arr);

    /* Request just the fields we need - no limit/sortfield/sortorder needed
     * since we're querying by a specific itemids filter */
    JsonValue *output_arr = json_new_array();
    json_array_push(output_arr, json_new_string("itemid"));
    json_array_push(output_arr, json_new_string("lastvalue"));
    json_array_push(output_arr, json_new_string("lastclock"));
    json_array_push(output_arr, json_new_string("units"));
    json_array_push(output_arr, json_new_string("name"));
    json_array_push(output_arr, json_new_string("value_type"));
    json_object_set(params, "output", output_arr);

    JsonValue *result = api_call(api, "item.get", params, 1);
    if (!result) {
        debug_log("  api_call returned NULL: %s", g_error);
        return -1;
    }

    if (result->type != JSON_ARRAY) {
        debug_log("  result is not array (type=%d)", result->type);
        json_free(result);
        return -1;
    }

    if (result->array.count == 0) {
        debug_log("  result array is empty (0 items)");
        json_free(result);
        set_error("Item not found or no access");
        return -1;
    }

    JsonValue *item = &result->array.items[0];
    const char *lv = json_string(json_object_get(item, "lastvalue"));
    const char *lc = json_string(json_object_get(item, "lastclock"));

    debug_log("  lastvalue='%s', lastclock='%s'",
              lv ? lv : "(null)", lc ? lc : "(null)");

    if (lv && lv[0]) {
        strncpy(lastvalue, lv, lastvalue_size - 1);
        lastvalue[lastvalue_size - 1] = '\0';
    } else {
        /* Item exists but has no value yet */
        strncpy(lastvalue, "N/A", lastvalue_size - 1);
        lastvalue[lastvalue_size - 1] = '\0';
        debug_log("  lastvalue is empty, using 'N/A'");
    }

    json_free(result);
    return 0;
}

int zabbix_api_get_item_info(ZabbixAPI *api, const char *item_id, ZabbixItem *info)
{
    memset(info, 0, sizeof(ZabbixItem));

    JsonValue *params = json_new_object();

    /* itemids as array for compatibility */
    JsonValue *item_arr = json_new_array();
    json_array_push(item_arr, json_new_string(item_id));
    json_object_set(params, "itemids", item_arr);

    JsonValue *output_arr = json_new_array();
    json_array_push(output_arr, json_new_string("itemid"));
    json_array_push(output_arr, json_new_string("name"));
    json_array_push(output_arr, json_new_string("key_"));
    json_array_push(output_arr, json_new_string("units"));
    json_array_push(output_arr, json_new_string("lastvalue"));
    json_array_push(output_arr, json_new_string("value_type"));
    json_object_set(params, "output", output_arr);

    JsonValue *result = api_call(api, "item.get", params, 1);
    if (!result || result->type != JSON_ARRAY || result->array.count == 0) {
        if (result) json_free(result);
        return -1;
    }

    JsonValue *item = &result->array.items[0];
    const char *name  = json_string(json_object_get(item, "name"));
    const char *key   = json_string(json_object_get(item, "key_"));
    const char *units = json_string(json_object_get(item, "units"));
    const char *lv    = json_string(json_object_get(item, "lastvalue"));
    JsonValue *vt     = json_object_get(item, "value_type");

    if (name)  strncpy(info->name, name, sizeof(info->name) - 1);
    if (key)   strncpy(info->key, key, sizeof(info->key) - 1);
    if (units) strncpy(info->units, units, sizeof(info->units) - 1);
    if (lv)    strncpy(info->lastvalue, lv, sizeof(info->lastvalue) - 1);
    info->value_type = (int)json_number(vt);

    strncpy(info->id, item_id, sizeof(info->id) - 1);
    json_free(result);
    return 0;
}

void zabbix_api_abort(ZabbixAPI *api)
{
    HINTERNET h = NULL;
    EnterCriticalSection(&api->request_cs);
    if (api->active_request) {
        h = api->active_request;
        api->active_request = NULL;
    }
    LeaveCriticalSection(&api->request_cs);
    if (h) {
        /* Closing the request handle from another thread forces the blocking
         * InternetReadFile/HttpSendRequest to fail immediately. */
        debug_log("Aborting in-flight HTTP request");
        InternetCloseHandle(h);
    }
}
