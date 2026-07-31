#ifndef I18N_H
#define I18N_H

#include <windows.h>

/* Language codes */
#define I18N_EN 0
#define I18N_ZH 1

/* String keys — every translatable UI string gets an entry */
typedef enum {
    /* ---- Login dialog ---- */
    S_ZABBIX_URL,
    S_HTTP_HTTPS,
    S_USERNAME,
    S_PASSWORD,
    S_LOGIN,
    S_CANCEL,
    S_BTN_OK,
    S_CONNECTING,
    S_FILL_URL_USER,
    S_LOGIN_FAILED_FMT,    /* "Login failed: %s" */
    S_LOGIN_TITLE,

    /* ---- Select dialog ---- */
    S_HOSTS,
    S_MONITOR_ITEMS,
    S_SEARCH,
    S_WIDGET_TYPE,
    S_GAUGE,
    S_CARD,
    S_TREND,
    S_LOADING_HOSTS,
    S_ADD_WIDGET,
    S_SELECT_ITEM_TITLE,
    S_FAILED_LOAD_HOSTS_FMT,  /* "Failed to load hosts: %s" */
    S_HOSTS_LOADED_FMT,       /* "%d hosts loaded. Select a host." */
    S_LOADING_ITEMS,
    S_FAILED_LOAD_ITEMS_FMT,  /* "Failed to load items: %s" */
    S_ITEMS_LOADED_FMT,      /* "%d items loaded." */
    S_SELECTED_FMT,          /* "Selected: %s (%s)" */
    S_NO_UNITS,
    S_SELECTED,

    /* ---- Widget context menu + settings ---- */
    S_ALWAYS_ON_TOP,
    S_LOCK_POSITION,
    S_CONFIGURE,
    S_REFRESH_NOW,
    S_REMOVE_WIDGET,
    S_WIDGET_SETTINGS_TITLE,
    S_REFRESH_SEC,
    S_CHANGE_MONITOR_ITEM,
    S_BACKGROUND_OPACITY,
    S_ACCENT_COLOR,
    S_GAUGE_SETTINGS,
    S_MIN,
    S_MAX,
    S_WARN,
    S_CRIT,
    S_TREND_SETTINGS,
    S_HOURS,
    S_LOADING,
    S_ERROR,

    /* ---- Tray menu + main ---- */
    S_ADD_WIDGET_ELLIPSIS,
    S_LOGIN_SETTINGS,
    S_REFRESH_ALL,
    S_UNLOCK_ALL,
    S_EXIT,
    S_TRAY_TIP,
    S_NOT_CONNECTED,
    S_FAILED_INIT_GDIPLUS,
    S_FAILED_REGISTER_CLASS,
    S_FAILED_CREATE_MAIN,
    S_RIGHT_CLICK_ADD,
    S_BALLOON_TITLE,
    S_ALREADY_RUNNING,

    S_COUNT
} StrKey;

/* Initialize: detect OS UI language and set active language */
void i18n_init(void);

/* Get the active language code (LANG_ENGLISH or LANG_CHINESE) */
int i18n_get_lang(void);

/* Override language manually (0=en, 1=zh) */
void i18n_set_lang(int lang);

/* Get a UTF-8 string by key. Never returns NULL. */
const char *i18n_str(StrKey key);

/* ---- Shared utility functions (moved here from ui_select.c) ---- */

/* Convert UTF-8 to wide (malloc'd, caller frees). Returns NULL if input NULL. */
wchar_t *utf8_to_wide(const char *utf8);

/* Convert wide to ANSI system code page (malloc'd). Returns NULL if input NULL. */
char *wide_to_acp(const wchar_t *w);

/* Convert UTF-8 to ANSI via wide (malloc'd). */
char *utf8_to_acp(const char *utf8);

/* Set a control's text from a UTF-8 string (uses SetWindowTextW internally) */
void set_text_utf8(HWND hCtrl, const char *utf8);

/* ---- Window-creation helpers (use CreateWindowExW for Unicode) ---- */

/* Create a static label with i18n text */
HWND i18n_create_label(HWND parent, StrKey key, int x, int y, int w, int h, HFONT font);

/* Create a static label with arbitrary UTF-8 text */
HWND i18n_create_label_str(HWND parent, const char *utf8, int x, int y, int w, int h, HFONT font);

/* Create a button with i18n text */
HWND i18n_create_button(HWND parent, StrKey key, DWORD style, int x, int y, int w, int h, int id, HFONT font);

/* Create a checkbox with i18n text */
HWND i18n_create_checkbox(HWND parent, StrKey key, int checked, int x, int y, int w, int h, int id, HFONT font);

/* Create an edit control (no i18n needed, just uniform) */
HWND i18n_create_edit(HWND parent, const char *text, int x, int y, int w, int h, int id, HFONT font);

/* Append a menu item with i18n text (uses AppendMenuW) */
void i18n_append_menu(HMENU menu, UINT flags, UINT_PTR id, StrKey key);

/* Append a menu item with arbitrary UTF-8 text */
void i18n_append_menu_str(HMENU menu, UINT flags, UINT_PTR id, const char *utf8);

/* Set window title from i18n key (uses SetWindowTextW) */
void i18n_set_window_title(HWND hwnd, StrKey key);

/* MessageBox with i18n text and title */
int i18n_message_box(HWND parent, StrKey text_key, StrKey title_key, UINT flags);

/* Format and set UTF-8 text (for strings with %s/%d format specifiers) */
void i18n_set_text_fmt(HWND hwnd, const char *fmt, ...);

#endif /* I18N_H */
