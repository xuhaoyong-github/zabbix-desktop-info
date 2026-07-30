#include "i18n.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* ============================================================
 *  Language detection & string table
 * ============================================================ */

static int g_lang = I18N_EN;

void i18n_init(void)
{
    LANGID lid = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(lid) == LANG_CHINESE)
        g_lang = I18N_ZH;
    else
        g_lang = I18N_EN;
}

int i18n_get_lang(void)  { return g_lang; }
void i18n_set_lang(int l) { g_lang = (l == I18N_ZH) ? I18N_ZH : I18N_EN; }

/* String table: two columns [en, zh], indexed by StrKey enum.
 * All Chinese strings are UTF-8 encoded. */
static const char *const g_strings[S_COUNT][2] = {
    /* ---- Login dialog ---- */
    [S_ZABBIX_URL]          = { "Zabbix URL:",             "Zabbix \xe5\x9c\xb0\xe5\x9d\x80\xef\xbc\x9a" },
    [S_HTTP_HTTPS]          = { "(http/https)",             "(http/https)" },
    [S_USERNAME]            = { "Username:",                "\xe7\x94\xa8\xe6\x88\xb7\xe5\x90\x8d\xef\xbc\x9a" },
    [S_PASSWORD]            = { "Password:",                "\xe5\xaf\x86\xe7\xa0\x81\xef\xbc\x9a" },
    [S_LOGIN]               = { "Login",                     "\xe7\x99\xbb\xe5\xbd\x95" },
    [S_CANCEL]              = { "Cancel",                   "\xe5\x8f\x96\xe6\xb6\x88" },
    [S_BTN_OK]             = { "OK",                       "\xe7\xa1\xae\xe5\xae\x9a" },
    [S_CONNECTING]          = { "Connecting...",             "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad..." },
    [S_FILL_URL_USER]       = { "Please fill in URL and username", "\xe8\xaf\xb7\xe5\xa1\xab\xe5\x86\x99\xe5\x9c\xb0\xe5\x9d\x80\xe5\x92\x8c\xe7\x94\xa8\xe6\x88\xb7\xe5\x90\x8d" },
    [S_LOGIN_FAILED_FMT]   = { "Login failed: %s",          "\xe7\x99\xbb\xe5\xbd\x95\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x9a%s" },
    [S_LOGIN_TITLE]         = { "Zabbix Login",              "Zabbix \xe7\x99\xbb\xe5\xbd\x95" },

    /* ---- Select dialog ---- */
    [S_HOSTS]               = { "Hosts:",                    "\xe4\xb8\xbb\xe6\x9c\xba\xef\xbc\x9a" },
    [S_MONITOR_ITEMS]      = { "Monitor Items:",            "\xe7\x9b\x91\xe6\x8e\xa7\xe9\xa1\xb9\xef\xbc\x9a" },
    [S_SEARCH]              = { "Search:",                   "\xe6\x90\x9c\xe7\xb4\xa2\xef\xbc\x9a" },
    [S_WIDGET_TYPE]         = { "Widget Type:",              "\xe7\xbb\x84\xe4\xbb\xb6\xe7\xb1\xbb\xe5\x9e\x8b\xef\xbc\x9a" },
    [S_GAUGE]               = { "Gauge",                     "\xe4\xbb\xaa\xe8\xa1\xa8\xe7\x9b\x98" },
    [S_CARD]                = { "Card",                      "\xe5\x8d\xa1\xe7\x89\x87" },
    [S_TREND]               = { "Trend",                    "\xe8\xb6\x8b\xe5\x8a\xbf\xe5\x9b\xbe" },
    [S_LOADING_HOSTS]      = { "Loading hosts...",          "\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8a\xa0\xe8\xbd\xbd\xe4\xb8\xbb\xe6\x9c\xba..." },
    [S_ADD_WIDGET]         = { "Add Widget",                "\xe6\xb7\xbb\xe5\x8a\xa0\xe7\xbb\x84\xe4\xbb\xb6" },
    [S_SELECT_ITEM_TITLE]   = { "Select Monitor Item",       "\xe9\x80\x89\xe6\x8b\xa9\xe7\x9b\x91\xe6\x8e\xa7\xe9\xa1\xb9" },
    [S_FAILED_LOAD_HOSTS_FMT] = { "Failed to load hosts: %s", "\xe5\x8a\xa0\xe8\xbd\xbd\xe4\xb8\xbb\xe6\x9c\xba\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x9a%s" },
    [S_HOSTS_LOADED_FMT]   = { "%d hosts loaded. Select a host.", "\xe5\xb7\xb2\xe5\x8a\xa0\xe8\xbd\xbd %d \xe4\xb8\xaa\xe4\xb8\xbb\xe6\x9c\xba\xef\xbc\x8c\xe8\xaf\xb7\xe9\x80\x89\xe6\x8b\xa9\xe3\x80\x82" },
    [S_LOADING_ITEMS]      = { "Loading items...",          "\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8a\xa0\xe8\xbd\xbd\xe7\x9b\x91\xe6\x8e\xa7\xe9\xa1\xb9..." },
    [S_FAILED_LOAD_ITEMS_FMT] = { "Failed to load items: %s", "\xe5\x8a\xa0\xe8\xbd\xbd\xe7\x9b\x91\xe6\x8e\xa7\xe9\xa1\xb9\xe5\xa4\xb1\xe8\xb4\xa5\xef\xbc\x9a%s" },
    [S_ITEMS_LOADED_FMT]   = { "%d items loaded.",           "\xe5\xb7\xb2\xe5\x8a\xa0\xe8\xbd\xbd %d \xe4\xb8\xaa\xe7\x9b\x91\xe6\x8e\xa7\xe9\xa1\xb9\xe3\x80\x82" },
    [S_SELECTED_FMT]       = { "Selected: %s (%s)",         "\xe5\xb7\xb2\xe9\x80\x89\xe6\x8b\xa9\xef\xbc\x9a%s (%s)" },
    [S_NO_UNITS]           = { "no units",                 "\xe6\x97\xa0\xe5\x8d\x95\xe4\xbd\x8d" },
    [S_SELECTED]           = { "Selected",                  "\xe5\xb7\xb2\xe9\x80\x89\xe6\x8b\xa9" },

    /* ---- Widget context menu + settings ---- */
    [S_ALWAYS_ON_TOP]     = { "Always on Top",              "\xe7\xbd\xae\xe9\xa1\xb6" },
    [S_LOCK_POSITION]     = { "Lock Position",              "\xe9\x94\x81\xe5\xae\x9a\xe4\xbd\x8d\xe7\xbd\xae" },
    [S_CONFIGURE]         = { "Configure...",               "\xe8\xae\xbe\xe7\xbd\xae..." },
    [S_REFRESH_NOW]       = { "Refresh Now",                "\xe7\xab\x8b\xe5\x8d\xb3\xe5\x88\xb7\xe6\x96\xb0" },
    [S_REMOVE_WIDGET]     = { "Remove Widget",              "\xe7\xa7\xbb\xe9\x99\xa4\xe7\xbb\x84\xe4\xbb\xb6" },
    [S_WIDGET_SETTINGS_TITLE] = { "Widget Settings",        "\xe7\xbb\x84\xe4\xbb\xb6\xe8\xae\xbe\xe7\xbd\xae" },
    [S_REFRESH_SEC]      = { "Refresh (sec):",              "\xe5\x88\xb7\xe6\x96\xb0\xe9\x97\xb4\xe9\x9a\x94(\xe7\xa7\x92)\xef\xbc\x9a" },
    [S_BACKGROUND_OPACITY] = { "Background Opacity:",       "\xe8\x83\x8c\xe6\x99\xaf\xe9\x80\x8f\xe6\x98\x8e\xe5\xba\xa6\xef\xbc\x9a" },
    [S_ACCENT_COLOR]     = { "Accent Color:",               "\xe5\xbc\xba\xe8\xb0\x83\xe8\x89\xb2\xef\xbc\x9a" },
    [S_GAUGE_SETTINGS]   = { "--- Gauge Settings ---",      "--- \xe4\xbb\xaa\xe8\xa1\xa8\xe7\x9b\x98\xe8\xae\xbe\xe7\xbd\xae ---" },
    [S_MIN]              = { "Min:",                       "\xe6\x9c\x80\xe5\xb0\x8f\xef\xbc\x9a" },
    [S_MAX]              = { "Max:",                       "\xe6\x9c\x80\xe5\xa4\xa7\xef\xbc\x9a" },
    [S_WARN]             = { "Warn:",                      "\xe8\xad\xa6\xe5\x91\x8a\xef\xbc\x9a" },
    [S_CRIT]             = { "Crit:",                      "\xe5\x8d\xb1\xe9\x99\xa9\xef\xbc\x9a" },
    [S_TREND_SETTINGS]   = { "--- Trend Settings ---",      "--- \xe8\xb6\x8b\xe5\x8a\xbf\xe5\x9b\xbe\xe8\xae\xbe\xe7\xbd\xae ---" },
    [S_HOURS]            = { "Hours:",                     "\xe6\x97\xb6\xe9\x97\xb4\xe8\x8c\x83\xe5\x9b\xb4(\xe5\xb0\x8f\xe6\x97\xb6)\xef\xbc\x9a" },
    [S_LOADING]          = { "Loading...",                  "\xe5\x8a\xa0\xe8\xbd\xbd\xe4\xb8\xad..." },
    [S_ERROR]            = { "Error",                      "\xe9\x94\x99\xe8\xaf\xaf" },

    /* ---- Tray menu + main ---- */
    [S_ADD_WIDGET_ELLIPSIS] = { "Add Widget...",            "\xe6\xb7\xbb\xe5\x8a\xa0\xe7\xbb\x84\xe4\xbb\xb6..." },
    [S_LOGIN_SETTINGS]     = { "Login Settings...",          "\xe7\x99\xbb\xe5\xbd\x95\xe8\xae\xbe\xe7\xbd\xae..." },
    [S_REFRESH_ALL]        = { "Refresh All",               "\xe5\x88\xb7\xe6\x96\xb0\xe5\x85\xa8\xe9\x83\xa8" },
    [S_UNLOCK_ALL]        = { "Unlock All",                 "\xe5\x85\xa8\xe9\x83\xa8\xe8\xa7\xa3\xe9\x94\x81" },
    [S_EXIT]              = { "Exit",                       "\xe9\x80\x80\xe5\x87\xba" },
    [S_TRAY_TIP]         = { "Zabbix Desktop Info",        "Zabbix \xe6\xa1\x8c\xe9\x9d\xa2\xe7\x9b\x91\xe6\x8e\xa7" },
    [S_NOT_CONNECTED]    = { "Not connected to Zabbix. Please login first.", "\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5\xe5\x88\xb0 Zabbix\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe7\x99\xbb\xe5\xbd\x95\xe3\x80\x82" },
    [S_FAILED_INIT_GDIPLUS] = { "Failed to initialize GDI+",  "GDI+ \xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96\xe5\xa4\xb1\xe8\xb4\xa5" },
    [S_FAILED_REGISTER_CLASS] = { "Failed to register window class", "\xe6\xb3\xa8\xe5\x86\x8c\xe7\xaa\x97\xe5\x8f\xa3\xe7\xb1\xbb\xe5\xa4\xb1\xe8\xb4\xa5" },
    [S_FAILED_CREATE_MAIN] = { "Failed to create main window", "\xe5\x88\x9b\xe5\xbb\xba\xe4\xb8\xbb\xe7\xaa\x97\xe5\x8f\xa3\xe5\xa4\xb1\xe8\xb4\xa5" },
    [S_RIGHT_CLICK_ADD]  = { "Right-click the tray icon to add widgets.", "\xe5\x8f\xb3\xe9\x94\xae\xe6\x89\x98\xe7\x9b\x98\xe5\x9b\xbe\xe6\xa0\x87\xe6\xb7\xbb\xe5\x8a\xa0\xe7\xbb\x84\xe4\xbb\xb6\xe3\x80\x82" },
    [S_BALLOON_TITLE]    = { "Zabbix Desktop Info",        "Zabbix \xe6\xa1\x8c\xe9\x9d\xa2\xe7\x9b\x91\xe6\x8e\xa7" },
};

const char *i18n_str(StrKey key)
{
    if (key < 0 || key >= S_COUNT)
        return "";
    const char *s = g_strings[key][g_lang];
    return s ? s : g_strings[key][I18N_EN];
}

/* ============================================================
 *  Shared utility functions (moved from ui_select.c)
 * ============================================================ */

wchar_t *utf8_to_wide(const char *utf8)
{
    if (!utf8) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, len);
    return w;
}

char *wide_to_acp(const wchar_t *w)
{
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *a = (char *)malloc(len);
    if (!a) return NULL;
    WideCharToMultiByte(CP_ACP, 0, w, -1, a, len, NULL, NULL);
    return a;
}

char *utf8_to_acp(const char *utf8)
{
    wchar_t *w = utf8_to_wide(utf8);
    if (!w) return NULL;
    char *a = wide_to_acp(w);
    free(w);
    return a;
}

void set_text_utf8(HWND hCtrl, const char *utf8)
{
    if (!utf8) { SetWindowTextA(hCtrl, ""); return; }
    wchar_t *w = utf8_to_wide(utf8);
    if (w) {
        SetWindowTextW(hCtrl, w);
        free(w);
    } else {
        SetWindowTextA(hCtrl, utf8);
    }
}

/* ============================================================
 *  Window-creation helpers (use W APIs for Unicode)
 * ============================================================ */

HWND i18n_create_label(HWND parent, StrKey key, int x, int y, int w, int h, HFONT font)
{
    return i18n_create_label_str(parent, i18n_str(key), x, y, w, h, font);
}

HWND i18n_create_label_str(HWND parent, const char *utf8, int x, int y, int w, int h, HFONT font)
{
    wchar_t *wtext = utf8_to_wide(utf8);
    HWND hw = CreateWindowExW(0, L"STATIC", wtext ? wtext : L"",
        WS_CHILD | WS_VISIBLE, x, y, w, h, parent, NULL, NULL, NULL);
    if (wtext) free(wtext);
    if (font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

HWND i18n_create_button(HWND parent, StrKey key, DWORD style, int x, int y, int w, int h, int id, HFONT font)
{
    wchar_t *wtext = utf8_to_wide(i18n_str(key));
    HWND hw = CreateWindowExW(0, L"BUTTON", wtext ? wtext : L"",
        WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    if (wtext) free(wtext);
    if (font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

HWND i18n_create_checkbox(HWND parent, StrKey key, int checked, int x, int y, int w, int h, int id, HFONT font)
{
    HWND hw = i18n_create_button(parent, key, BS_AUTOCHECKBOX, x, y, w, h, id, font);
    SendMessageW(hw, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return hw;
}

HWND i18n_create_edit(HWND parent, const char *text, int x, int y, int w, int h, int id, HFONT font)
{
    wchar_t *wtext = (text && text[0]) ? utf8_to_wide(text) : NULL;
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wtext ? wtext : L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    if (wtext) free(wtext);
    if (font) SendMessageW(hw, WM_SETFONT, (WPARAM)font, TRUE);
    return hw;
}

void i18n_append_menu(HMENU menu, UINT flags, UINT_PTR id, StrKey key)
{
    i18n_append_menu_str(menu, flags, id, i18n_str(key));
}

void i18n_append_menu_str(HMENU menu, UINT flags, UINT_PTR id, const char *utf8)
{
    wchar_t *wtext = utf8_to_wide(utf8);
    if (wtext) {
        AppendMenuW(menu, flags, id, wtext);
        free(wtext);
    } else {
        AppendMenuA(menu, flags, id, utf8 ? utf8 : "");
    }
}

void i18n_set_window_title(HWND hwnd, StrKey key)
{
    wchar_t *wtext = utf8_to_wide(i18n_str(key));
    if (wtext) {
        SetWindowTextW(hwnd, wtext);
        free(wtext);
    }
}

int i18n_message_box(HWND parent, StrKey text_key, StrKey title_key, UINT flags)
{
    wchar_t *wtext = utf8_to_wide(i18n_str(text_key));
    wchar_t *wtitle = utf8_to_wide(i18n_str(title_key));
    int ret = MessageBoxW(parent, wtext ? wtext : L"", wtitle ? wtitle : L"", flags);
    if (wtext) free(wtext);
    if (wtitle) free(wtitle);
    return ret;
}

void i18n_set_text_fmt(HWND hwnd, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    set_text_utf8(hwnd, buf);
}
