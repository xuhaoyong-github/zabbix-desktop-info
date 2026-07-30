#include "config.h"
#include "json.h"
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "crypt32.lib")

/* ============ DPAPI Password Protection ============ */
/* Uses Windows Data Protection API (CryptProtectData/CryptUnprotectData).
 * The password is encrypted using the current Windows user's credentials.
 * The encrypted blob can ONLY be decrypted by the same user on the same machine.
 * If someone copies the config file to another machine or user, they cannot
 * recover the password. This is the Windows-native standard for protecting
 * secrets (browsers, email clients, etc. use this).
 *
 * Note: True one-way hashing (SHA-256 etc.) is incompatible with Zabbix's
 * user.login API which requires the plaintext password. DPAPI achieves
 * equivalent protection: the stored data is unreadable without the user's
 * Windows login session.
 */

/* Hex-encode binary data to a string (caller frees) */
static char *hex_encode(const unsigned char *data, DWORD len)
{
    char *out = (char *)malloc(len * 2 + 1);
    if (!out) return NULL;
    const char hexchars[] = "0123456789ABCDEF";
    for (DWORD i = 0; i < len; i++) {
        out[i * 2]     = hexchars[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = hexchars[data[i] & 0xF];
    }
    out[len * 2] = '\0';
    return out;
}

/* Hex-decode a string to binary data (caller frees), sets *out_len */
static unsigned char *hex_decode(const char *str, DWORD *out_len)
{
    DWORD slen = (DWORD)strlen(str);
    if (slen % 2 != 0) return NULL;
    *out_len = slen / 2;
    if (*out_len == 0) return NULL;
    unsigned char *out = (unsigned char *)malloc(*out_len);
    if (!out) return NULL;
    for (DWORD i = 0; i < *out_len; i++) {
        int hi, lo;
        char c;
        c = str[i * 2];
             if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else { free(out); return NULL; }
        c = str[i * 2 + 1];
             if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else { free(out); return NULL; }
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return out;
}

/* Encrypt password with DPAPI, returns hex string (caller frees, NULL on error) */
static char *dpapi_encrypt(const char *plaintext)
{
    if (!plaintext || !plaintext[0]) return NULL;

    DATA_BLOB in_blob, out_blob;
    in_blob.pbData = (BYTE *)plaintext;
    in_blob.cbData = (DWORD)(strlen(plaintext) + 1); /* include null terminator */

    if (!CryptProtectData(&in_blob, L"ZabbixDesktop", NULL, NULL, NULL, 0, &out_blob))
        return NULL;

    char *hex = hex_encode(out_blob.pbData, out_blob.cbData);
    LocalFree(out_blob.pbData);
    return hex;
}

/* Decrypt password with DPAPI, returns plaintext (caller frees, NULL on error) */
static char *dpapi_decrypt(const char *hex)
{
    DWORD enc_len = 0;
    unsigned char *enc_data = hex_decode(hex, &enc_len);
    if (!enc_data) return NULL;

    DATA_BLOB in_blob, out_blob;
    in_blob.pbData = enc_data;
    in_blob.cbData = enc_len;

    LPWSTR description = NULL;
    BOOL ok = CryptUnprotectData(&in_blob, &description, NULL, NULL, NULL, 0, &out_blob);
    free(enc_data);

    if (!ok) return NULL;
    if (description) LocalFree(description);

    /* out_blob.pbData includes the null terminator we encrypted */
    char *result = _strdup((char *)out_blob.pbData);
    LocalFree(out_blob.pbData);
    return result;
}

void config_init(AppConfig *cfg)
{
    memset(cfg, 0, sizeof(AppConfig));
    cfg->widget_count = 0;
}

void config_get_default_path(char *path, int path_size)
{
    char appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\ZabbixDesktopInfo", appdata);
        CreateDirectoryA(dir, NULL);
        snprintf(path, path_size, "%s\\config.json", dir);
    } else {
        strncpy(path, "config.json", path_size - 1);
    }
}

/* Parse a widget config from JSON */
static void parse_widget(JsonValue *jw, WidgetConfig *wc)
{
    memset(wc, 0, sizeof(WidgetConfig));

    wc->type = (WidgetType)(int)json_number(json_object_get(jw, "type"));
    const char *item_id = json_string(json_object_get(jw, "item_id"));
    const char *host_name = json_string(json_object_get(jw, "host_name"));
    const char *item_name = json_string(json_object_get(jw, "item_name"));
    const char *units = json_string(json_object_get(jw, "units"));

    if (item_id)   strncpy(wc->item_id, item_id, sizeof(wc->item_id) - 1);
    if (host_name) strncpy(wc->host_name, host_name, sizeof(wc->host_name) - 1);
    if (item_name) strncpy(wc->item_name, item_name, sizeof(wc->item_name) - 1);
    if (units)     strncpy(wc->units, units, sizeof(wc->units) - 1);

    wc->value_type = (int)json_number(json_object_get(jw, "value_type"));
    wc->x = (int)json_number(json_object_get(jw, "x"));
    wc->y = (int)json_number(json_object_get(jw, "y"));
    wc->width = (int)json_number(json_object_get(jw, "width"));
    wc->height = (int)json_number(json_object_get(jw, "height"));

    wc->always_on_top = (int)json_number(json_object_get(jw, "always_on_top"));
    wc->refresh_interval = (int)json_number(json_object_get(jw, "refresh_interval"));
    if (wc->refresh_interval <= 0) wc->refresh_interval = 30;

    wc->gauge_min = json_number(json_object_get(jw, "gauge_min"));
    wc->gauge_max = json_number(json_object_get(jw, "gauge_max"));
    wc->gauge_warn = json_number(json_object_get(jw, "gauge_warn"));
    wc->gauge_crit = json_number(json_object_get(jw, "gauge_crit"));
    wc->gauge_warn_enabled = (int)json_number(json_object_get(jw, "gauge_warn_enabled"));
    wc->gauge_crit_enabled = (int)json_number(json_object_get(jw, "gauge_crit_enabled"));

    wc->trend_hours = (int)json_number(json_object_get(jw, "trend_hours"));
    if (wc->trend_hours <= 0) wc->trend_hours = 1;

    wc->bg_opacity = (int)json_number(json_object_get(jw, "bg_opacity"));
    if (wc->bg_opacity == 0) wc->bg_opacity = 200;

    wc->accent_color = (int)json_number(json_object_get(jw, "accent_color"));
    if (wc->accent_color == 0) wc->accent_color = 0x4A90D9; /* default blue */

    /* Default size if not set */
    if (wc->width <= 0) {
        switch (wc->type) {
            case WIDGET_GAUGE: wc->width = 200; wc->height = 220; break;
            case WIDGET_CARD:  wc->width = 240; wc->height = 100; break;
            case WIDGET_TREND: wc->width = 320; wc->height = 180; break;
        }
    }
}

static JsonValue *widget_to_json(const WidgetConfig *wc)
{
    JsonValue *jw = json_new_object();
    json_object_set(jw, "type", json_new_number((double)wc->type));
    json_object_set(jw, "item_id", json_new_string(wc->item_id));
    json_object_set(jw, "host_name", json_new_string(wc->host_name));
    json_object_set(jw, "item_name", json_new_string(wc->item_name));
    json_object_set(jw, "units", json_new_string(wc->units));
    json_object_set(jw, "value_type", json_new_number((double)wc->value_type));
    json_object_set(jw, "x", json_new_number((double)wc->x));
    json_object_set(jw, "y", json_new_number((double)wc->y));
    json_object_set(jw, "width", json_new_number((double)wc->width));
    json_object_set(jw, "height", json_new_number((double)wc->height));
    json_object_set(jw, "always_on_top", json_new_number((double)wc->always_on_top));
    json_object_set(jw, "refresh_interval", json_new_number((double)wc->refresh_interval));
    json_object_set(jw, "gauge_min", json_new_number(wc->gauge_min));
    json_object_set(jw, "gauge_max", json_new_number(wc->gauge_max));
    json_object_set(jw, "gauge_warn", json_new_number(wc->gauge_warn));
    json_object_set(jw, "gauge_crit", json_new_number(wc->gauge_crit));
    json_object_set(jw, "gauge_warn_enabled", json_new_number((double)wc->gauge_warn_enabled));
    json_object_set(jw, "gauge_crit_enabled", json_new_number((double)wc->gauge_crit_enabled));
    json_object_set(jw, "trend_hours", json_new_number((double)wc->trend_hours));
    json_object_set(jw, "bg_opacity", json_new_number((double)wc->bg_opacity));
    json_object_set(jw, "accent_color", json_new_number((double)wc->accent_color));
    return jw;
}

int config_load(AppConfig *cfg, const char *filepath)
{
    config_init(cfg);

    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) { fclose(f); return -1; }

    char *text = (char *)malloc(size + 1);
    fread(text, 1, size, f);
    text[size] = '\0';
    fclose(f);

    JsonValue *root = json_parse(text);
    free(text);

    if (!root || root->type != JSON_OBJECT) {
        if (root) json_free(root);
        return -1;
    }

    const char *url  = json_string(json_object_get(root, "zabbix_url"));
    const char *user = json_string(json_object_get(root, "zabbix_user"));
    const char *pass = json_string(json_object_get(root, "zabbix_pass"));
    if (url)  strncpy(cfg->zabbix_url, url, sizeof(cfg->zabbix_url) - 1);
    if (user) strncpy(cfg->zabbix_user, user, sizeof(cfg->zabbix_user) - 1);
    if (pass) {
        /* Check if password is DPAPI-encrypted (starts with "enc:") */
        if (strncmp(pass, "enc:", 4) == 0) {
            char *dec = dpapi_decrypt(pass + 4);
            if (dec) {
                strncpy(cfg->zabbix_pass, dec, sizeof(cfg->zabbix_pass) - 1);
                cfg->zabbix_pass[sizeof(cfg->zabbix_pass) - 1] = '\0';
                free(dec);
            } else {
                /* Decryption failed: different user/machine, or data corruption.
                 * Clear password so user must re-enter. */
                cfg->zabbix_pass[0] = '\0';
            }
        } else {
            /* Old plaintext format — backward compatible */
            strncpy(cfg->zabbix_pass, pass, sizeof(cfg->zabbix_pass) - 1);
        }
    }

    JsonValue *widgets = json_object_get(root, "widgets");
    if (widgets && widgets->type == JSON_ARRAY) {
        int count = widgets->array.count;
        if (count > MAX_WIDGETS) count = MAX_WIDGETS;
        for (int i = 0; i < count; i++) {
            parse_widget(&widgets->array.items[i], &cfg->widgets[i]);
        }
        cfg->widget_count = count;
    }

    json_free(root);
    return 0;
}

int config_save(const AppConfig *cfg, const char *filepath)
{
    JsonValue *root = json_new_object();
    json_object_set(root, "zabbix_url", json_new_string(cfg->zabbix_url));
    json_object_set(root, "zabbix_user", json_new_string(cfg->zabbix_user));

    /* Encrypt password with DPAPI before storing */
    if (cfg->zabbix_pass[0]) {
        char *enc = dpapi_encrypt(cfg->zabbix_pass);
        if (enc) {
            /* Prefix with "enc:" so we know it's encrypted on load */
            char *enc_field = (char *)malloc(strlen(enc) + 8);
            if (enc_field) {
                sprintf(enc_field, "enc:%s", enc);
                json_object_set(root, "zabbix_pass", json_new_string(enc_field));
                free(enc_field);
            } else {
                json_object_set(root, "zabbix_pass", json_new_string(cfg->zabbix_pass));
            }
            free(enc);
        } else {
            /* DPAPI failed — fall back to plaintext (shouldn't happen) */
            json_object_set(root, "zabbix_pass", json_new_string(cfg->zabbix_pass));
        }
    } else {
        json_object_set(root, "zabbix_pass", json_new_string(""));
    }

    JsonValue *widgets_arr = json_new_array();
    for (int i = 0; i < cfg->widget_count; i++) {
        JsonValue *jw = widget_to_json(&cfg->widgets[i]);
        json_array_push(widgets_arr, jw);
        /* Do NOT json_free(jw) here — json_array_push already
         * consumed it (move semantics: copies the struct, frees the pointer) */
    }
    json_object_set(root, "widgets", widgets_arr);
    /* Do NOT json_free(widgets_arr) here — json_object_set already
     * consumed it (same move semantics) */

    char *text = json_stringify(root, 1);
    json_free(root);

    FILE *f = fopen(filepath, "wb");
    if (!f) { free(text); return -1; }
    fputs(text, f);
    fclose(f);
    free(text);
    return 0;
}

int config_add_widget(AppConfig *cfg, const WidgetConfig *wc)
{
    if (cfg->widget_count >= MAX_WIDGETS) return -1;
    cfg->widgets[cfg->widget_count] = *wc;
    return cfg->widget_count++;
}

void config_remove_widget(AppConfig *cfg, int index)
{
    if (index < 0 || index >= cfg->widget_count) return;
    for (int i = index; i < cfg->widget_count - 1; i++)
        cfg->widgets[i] = cfg->widgets[i + 1];
    cfg->widget_count--;
}
