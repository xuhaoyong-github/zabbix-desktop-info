#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "widget.h"
#include "render.h"
#include "config.h"
#include "zabbix_api.h"
#include "i18n.h"
#include "resource.h"
#include "ui_select.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

/* ============ MULTI-MONITOR HELPERS ============ */

/* Get the work area of the monitor that contains the given point.
 * Falls back to primary monitor if point is off-screen. */
static void get_monitor_workarea_at(POINT pt, RECT *wa)
{
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(hMon, &mi)) {
        *wa = mi.rcWork;
    } else {
        SystemParametersInfoA(SPI_GETWORKAREA, 0, wa, 0);
    }
}

/* Check if a window rectangle is at least 25% visible on any monitor */
static int is_position_visible(int x, int y, int width, int height)
{
    POINT pt = { x + width / 2, y + height / 2 };
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    if (!hMon) return 0;

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hMon, &mi)) return 0;

    RECT wa = mi.rcWork;
    int vis_left   = (x > wa.left) ? x : wa.left;
    int vis_right  = (x + width < wa.right) ? (x + width) : wa.right;
    int vis_top    = (y > wa.top) ? y : wa.top;
    int vis_bottom = (y + height < wa.bottom) ? (y + height) : wa.bottom;

    if (vis_right <= vis_left || vis_bottom <= vis_top) return 0;
    int vis_area = (vis_right - vis_left) * (vis_bottom - vis_top);
    int total = width * height;
    return vis_area > total / 4;
}

/* Center a widget on the monitor where the mouse cursor is */
static void center_on_cursor_monitor(int *x, int *y, int width, int height)
{
    POINT cursor;
    GetCursorPos(&cursor);
    RECT wa;
    get_monitor_workarea_at(cursor, &wa);
    *x = wa.left + (wa.right - wa.left - width) / 2;
    *y = wa.top + (wa.bottom - wa.top - height) / 2;
}

#define WM_DATA_UPDATED   (WM_USER + 1)
#define WM_REFRESH_WIDGET (WM_USER + 2)

#define IDM_TOGGLE_TOPMOST  2001
#define IDM_CONFIGURE       2002
#define IDM_REFRESH_NOW     2003
#define IDM_REMOVE          2004
#define IDM_LOCK_POSITION   2005

/* Cached desktop host window so Win+D "Show Desktop" never minimizes widgets */
static HWND g_desktop_host = NULL;

/* Locate the desktop window that hosts the desktop icons (WorkerW / SHELLDLL_DefView /
 * Progman). Pinning a widget as a child of this window makes it part of the desktop,
 * so "Show Desktop" (Win+D) no longer minimizes it. */
static HWND FindDesktopHost(void)
{
    HWND progman = FindWindowA("Progman", "Program Manager");
    if (!progman) return NULL;
    /* Enumerate WorkerW windows; the one hosting SHELLDLL_DefView is the real desktop */
    HWND hw = NULL, worker = NULL;
    while ((hw = FindWindowExA(NULL, hw, "WorkerW", NULL)) != NULL) {
        if (FindWindowExA(hw, NULL, "SHELLDLL_DefView", NULL)) {
            worker = hw;
            break;
        }
    }
    if (worker) return worker;
    if (FindWindowExA(progman, NULL, "SHELLDLL_DefView", NULL))
        return progman;
    return progman;
}

/* Move a widget to a SCREEN position, converting to parent-client coordinates
 * if the widget is currently parented to the desktop host. */
static void move_widget_to_screen_pos(HWND hwnd, int sx, int sy)
{
    POINT pt = { sx, sy };
    HWND parent = GetAncestor(hwnd, GA_PARENT);
    if (parent && parent != GetDesktopWindow())
        ScreenToClient(parent, &pt);
    SetWindowPos(hwnd, NULL, pt.x, pt.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Apply pin mode:
 *  - topmost   : top-level window + HWND_TOPMOST (floats above everything)
 *  - !topmost  : child of the desktop host (pinned to desktop, Win+D immune)
 * Screen position is preserved across re-parenting. */
static void widget_apply_pin(HWND hwnd, int topmost)
{
    RECT rc;
    GetWindowRect(hwnd, &rc); /* always screen coords */

    if (topmost) {
        if (GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow())
            SetParent(hwnd, NULL);
        SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        if (!g_desktop_host || !IsWindow(g_desktop_host))
            g_desktop_host = FindDesktopHost();
        if (g_desktop_host) {
            if (GetAncestor(hwnd, GA_PARENT) != g_desktop_host)
                SetParent(hwnd, g_desktop_host);
            POINT pt = { rc.left, rc.top };
            ScreenToClient(g_desktop_host, &pt);
            /* HWND_TOP within the desktop layer keeps the widget above the
             * icon layer (SHELLDLL_DefView) but below all normal windows. */
            SetWindowPos(hwnd, HWND_TOP, pt.x, pt.y, 0, 0,
                         SWP_NOACTIVATE | SWP_NOSIZE | SWP_SHOWWINDOW);
        } else {
            /* No desktop host found: stay top-level, bottom of z-order */
            SetWindowPos(hwnd, HWND_BOTTOM, rc.left, rc.top, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
}

/* Settings dialog control IDs */
#define IDC_REFRESH_EDIT    3001
#define IDC_TOPMOST_CHECK   3002
#define IDC_OPACITY_TRACK   3003
#define IDC_OPACITY_VALUE   3004
#define IDC_MIN_EDIT        3005
#define IDC_MAX_EDIT        3006
#define IDC_WARN_EDIT       3007
#define IDC_WARN_CHECK      3008
#define IDC_CRIT_EDIT       3009
#define IDC_CRIT_CHECK      3010
#define IDC_HOURS_EDIT      3011
#define IDC_COLOR_BUTTON    3012
#define IDC_OK              3013
#define IDC_CANCEL          3014
#define IDC_CHANGE_ITEM     3015

typedef struct {
    WidgetConfig config;
    ZabbixAPI *api;
    AppConfig *app_config;
    int config_index;
    HWND hwnd;

    HANDLE thread;
    HANDLE refresh_event;  /* signaled to wake up worker early */
    int running;
    CRITICAL_SECTION cs;

    /* Current data */
    double current_value;
    char current_value_str[128];
    ZabbixHistoryPoint *history;
    int history_count;
    int data_valid;
    int fetch_failed;
    char error_msg[256];

    /* Rendering resources */
    HDC render_dc;
    HBITMAP render_bmp;
    void *bits;
    int width, height;

    /* Main window for notifications */
    HWND hMainWnd;

    /* Dragging */
    int dragging;
    POINT drag_start;
    POINT drag_origin;
} WidgetData;

/* Sync widget's local config back to app_config and save to file */
static void sync_and_save(WidgetData *wd)
{
    if (!wd || !wd->app_config) return;
    if (wd->config_index >= 0 && wd->config_index < wd->app_config->widget_count) {
        wd->app_config->widgets[wd->config_index] = wd->config;
    }
    char path[MAX_PATH];
    config_get_default_path(path, sizeof(path));
    config_save(wd->app_config, path);
}

/* ============ WORKER THREAD ============ */

static DWORD WINAPI widget_worker(LPVOID param)
{
    WidgetData *wd = (WidgetData *)param;

    while (wd->running) {
        /* Snapshot config under lock. The settings dialog (UI thread) writes
         * these fields without the lock otherwise, which would let us read a
         * torn copy of e.g. item_id here. */
        WidgetConfig cfg;
        EnterCriticalSection(&wd->cs);
        cfg = wd->config;
        LeaveCriticalSection(&wd->cs);

        if (wd->api && wd->api->connected) {
            char lastvalue[128] = {0};

            /* Fetch last value */
            int ret = zabbix_api_get_last_value(wd->api, cfg.item_id,
                                                lastvalue, sizeof(lastvalue));
            if (ret == 0) {
                EnterCriticalSection(&wd->cs);
                strncpy(wd->current_value_str, lastvalue, sizeof(wd->current_value_str) - 1);
                wd->current_value_str[sizeof(wd->current_value_str) - 1] = '\0';
                wd->current_value = atof(lastvalue);
                wd->data_valid = 1;
                wd->fetch_failed = 0;
                wd->error_msg[0] = '\0';
                LeaveCriticalSection(&wd->cs);
            } else {
                /* Record error for display */
                const char *err = zabbix_api_error();
                EnterCriticalSection(&wd->cs);
                wd->fetch_failed = 1;
                strncpy(wd->error_msg, err ? err : "Unknown error",
                        sizeof(wd->error_msg) - 1);
                wd->error_msg[sizeof(wd->error_msg) - 1] = '\0';
                LeaveCriticalSection(&wd->cs);
            }

            /* Fetch history for trend charts */
            if (cfg.type == WIDGET_TREND && cfg.value_type >= 0) {
                time_t now = time(NULL);
                time_t from = now - cfg.trend_hours * 3600;
                ZabbixHistoryPoint *points = NULL;
                int count = zabbix_api_get_history(wd->api, cfg.item_id,
                                                    cfg.value_type,
                                                    (int)from, (int)now, &points);
                if (count > 0) {
                    EnterCriticalSection(&wd->cs);
                    if (wd->history) free(wd->history);
                    wd->history = points;
                    wd->history_count = count;
                    LeaveCriticalSection(&wd->cs);
                }
            }

            PostMessage(wd->hwnd, WM_DATA_UPDATED, 0, 0);
        }

        /* Wait, checking running flag - wakeable by refresh_event */
        int interval = cfg.refresh_interval > 0 ? cfg.refresh_interval : 30;
        if (wd->refresh_event) {
            /* Wait for either the interval to elapse or a refresh signal */
            for (int i = 0; i < interval && wd->running; i++)
                WaitForSingleObject(wd->refresh_event, 1000);
            ResetEvent(wd->refresh_event);
        } else {
            for (int i = 0; i < interval && wd->running; i++)
                Sleep(1000);
        }
    }

    /* Thread is exiting. We own wd from here on: WM_DESTROY only waits on the
       thread handle and must NOT touch wd after we return. Free everything
       (including the GDI render buffers) here so nothing is leaked and nothing
       is double-freed. */
    if (wd->render_dc) DeleteDC(wd->render_dc);
    if (wd->render_bmp) DeleteObject(wd->render_bmp);
    if (wd->history) free(wd->history);
    if (wd->api) zabbix_api_abort(wd->api);   /* last-chance interrupt */
    DeleteCriticalSection(&wd->cs);
    if (wd->refresh_event) CloseHandle(wd->refresh_event);
    free(wd);
    return 0;
}

/* ============ RENDERING ============ */

static void widget_paint(WidgetData *wd)
{
    /* Free previous render */
    if (wd->render_dc) { DeleteDC(wd->render_dc); wd->render_dc = NULL; }
    if (wd->render_bmp) { DeleteObject(wd->render_bmp); wd->render_bmp = NULL; }
    wd->bits = NULL;

    int w = wd->config.width;
    int h = wd->config.height;

    EnterCriticalSection(&wd->cs);
    double val = wd->current_value;
    const char *value_str;
    if (wd->data_valid) {
        value_str = wd->current_value_str;
    } else if (wd->fetch_failed) {
        value_str = wd->error_msg[0] ? wd->error_msg : i18n_str(S_ERROR);
    } else {
        value_str = i18n_str(S_LOADING);
    }
    ZabbixHistoryPoint *hist = NULL;
    int hist_count = 0;
    if (wd->history && wd->history_count > 0) {
        hist = (ZabbixHistoryPoint *)malloc(wd->history_count * sizeof(ZabbixHistoryPoint));
        memcpy(hist, wd->history, wd->history_count * sizeof(ZabbixHistoryPoint));
        hist_count = wd->history_count;
    }
    LeaveCriticalSection(&wd->cs);

    render_widget(&wd->render_dc, &wd->render_bmp, w, h,
                  wd->config.type, val, value_str,
                  wd->config.item_name, wd->config.units,
                  &wd->config, hist, hist_count);

    if (hist) free(hist);

    /* Get DIB bits for hit testing */
    if (wd->render_bmp) {
        DIBSECTION ds;
        if (GetObject(wd->render_bmp, sizeof(DIBSECTION), &ds))
            wd->bits = ds.dsBm.bmBits;
    }
    wd->width = w;
    wd->height = h;

    /* Update layered window */
    HDC screenDC = GetDC(NULL);
    POINT ptZero = {0, 0};
    SIZE size = {w, h};
    BLENDFUNCTION blend;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(wd->hwnd, screenDC, NULL, &size,
                        wd->render_dc, &ptZero, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screenDC);
}

/* ============ CONTEXT MENU ============ */

static void show_context_menu(HWND hwnd, WidgetData *wd, int x, int y)
{
    HMENU hMenu = CreatePopupMenu();
    i18n_append_menu(hMenu, MF_STRING | (wd->config.always_on_top ? MF_CHECKED : 0),
                IDM_TOGGLE_TOPMOST, S_ALWAYS_ON_TOP);
    i18n_append_menu(hMenu, MF_STRING | (wd->config.lock_position ? MF_CHECKED : 0),
                IDM_LOCK_POSITION, S_LOCK_POSITION);
    i18n_append_menu(hMenu, MF_STRING, IDM_CONFIGURE, S_CONFIGURE);
    i18n_append_menu(hMenu, MF_STRING, IDM_REFRESH_NOW, S_REFRESH_NOW);
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    i18n_append_menu(hMenu, MF_STRING, IDM_REMOVE, S_REMOVE_WIDGET);

    /* The widget is non-activatable (WS_EX_NOACTIVATE); bring it to the
     * foreground so the popup menu dismisses correctly when clicking away */
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

/* ============ SETTINGS DIALOG ============ */

typedef struct {
    WidgetData *wd;
    int accent_color;
    HWND hOpacityLabel;
    HWND hCurrLabel;     /* "host / item" display label */
    /* Dark theme */
    HBRUSH hBgBrush;
    HBRUSH hEditBrush;
    HBRUSH hHeaderBrush;
    HFONT  hTitleFont;
} SettingsData;

/* ---- shared dark palette ---- */
#define SET_CLR_BG         RGB(30,  33,  48)
#define SET_CLR_HEADER_BG  RGB(24,  27,  40)
#define SET_CLR_TEXT       RGB(228, 228, 240)
#define SET_CLR_ACCENT     RGB(74,  144, 217)
#define SET_CLR_EDIT_BG    RGB(22,  24,  36)
#define SET_CLR_SEP        RGB(60,  64,  84)
#define SET_HEADER_Y 82
#define SET_MARGIN   12

static HFONT MakeSettingsTitleFont(void)
{
    return CreateFontA(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

static HWND CreateEdit(HWND parent, const char *text, int x, int y, int w, int h, int id, HFONT hFont)
{
    HWND hw = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", text, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
    return hw;
}

static HWND CreateTrackbar(HWND parent, int min, int max, int val, int x, int y, int w, int id, HFONT hFont)
{
    HWND hw = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
        x, y, w, 25, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(hw, TBM_SETRANGE, TRUE, MAKELONG(min, max));
    SendMessageA(hw, TBM_SETPOS, TRUE, val);
    return hw;
}

static HFONT GetDlgFont(void)
{
    /* Match the system tray context-menu font size exactly (same as ui_select.c) */
    NONCLIENTMETRICS ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        return CreateFontIndirectA(&ncm.lfMenuFont);
    /* Fallback: approximate menu font (~12px character height) */
    return CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

static BOOL CALLBACK SetChildFontProc(HWND child, LPARAM lp)
{
    SendMessageA(child, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

static void DrawColorSwatch(HWND hwnd, HDC hdc, int color)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
    /* Border - subtle on dark bg, clean on light */
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 105, 120));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsData *sd = (SettingsData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            sd = (SettingsData *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)sd);

            /* Dark theme brushes */
            sd->hBgBrush     = CreateSolidBrush(SET_CLR_BG);
            sd->hEditBrush   = CreateSolidBrush(SET_CLR_EDIT_BG);
            sd->hHeaderBrush = CreateSolidBrush(SET_CLR_HEADER_BG);
            sd->hTitleFont   = MakeSettingsTitleFont();

            HFONT hFont = GetDlgFont();
            WidgetConfig *cfg = &sd->wd->config;
            int isGauge = (cfg->type == WIDGET_GAUGE);
            int isTrend = (cfg->type == WIDGET_TREND);
            int y = SET_HEADER_Y + 8;
            int x1 = 15;
            int editW = 65;

            /* ---- Current Monitor Item ---- */
            i18n_create_label(hwnd, S_MONITOR_ITEMS, x1, y + 3, 130, 18, hFont);

            char buf[512];
            snprintf(buf, sizeof(buf), "%s / %s", cfg->host_name, cfg->item_name);
            sd->hCurrLabel = i18n_create_label_str(hwnd, buf, x1, y + 22, 265, 18, hFont);

            i18n_create_button(hwnd, S_CHANGE_MONITOR_ITEM,
                BS_PUSHBUTTON, x1, y + 44, 170, 22, IDC_CHANGE_ITEM, hFont);
            y += 76;

            /* Refresh interval */
            i18n_create_label(hwnd, S_REFRESH_SEC, x1, y + 3, 120, 18, hFont);
            snprintf(buf, sizeof(buf), "%d", cfg->refresh_interval);
            CreateEdit(hwnd, buf, 135, y, editW, 22, IDC_REFRESH_EDIT, hFont);
            y += 32;

            /* Always on top */
            i18n_create_checkbox(hwnd, S_ALWAYS_ON_TOP, cfg->always_on_top,
                          x1, y, 220, 20, IDC_TOPMOST_CHECK, hFont);
            y += 30;

            /* Opacity */
            i18n_create_label(hwnd, S_BACKGROUND_OPACITY, x1, y + 3, 140, 18, hFont);
            sd->hOpacityLabel = i18n_create_label_str(hwnd, "", 155, y + 3, 35, 18, hFont);
            CreateTrackbar(hwnd, 50, 255, cfg->bg_opacity, x1, y + 22, 200, IDC_OPACITY_TRACK, hFont);
            y += 55;

            /* Accent color */
            i18n_create_label(hwnd, S_ACCENT_COLOR, x1, y + 3, 90, 18, hFont);
            CreateWindowExA(0, "BUTTON", "", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                115, y, 44, 22, hwnd, (HMENU)(INT_PTR)IDC_COLOR_BUTTON, NULL, NULL);
            sd->accent_color = cfg->accent_color;
            y += 35;

            /* Gauge-specific */
            if (isGauge) {
                i18n_create_label(hwnd, S_GAUGE_SETTINGS, x1, y, 200, 18, hFont);
                y += 25;

                i18n_create_label(hwnd, S_MIN, x1, y + 3, 40, 18, hFont);
                snprintf(buf, sizeof(buf), "%.1f", cfg->gauge_min);
                CreateEdit(hwnd, buf, 55, y, editW, 22, IDC_MIN_EDIT, hFont);
                i18n_create_label(hwnd, S_MAX, 130, y + 3, 40, 18, hFont);
                snprintf(buf, sizeof(buf), "%.1f", cfg->gauge_max);
                CreateEdit(hwnd, buf, 175, y, editW, 22, IDC_MAX_EDIT, hFont);
                y += 30;

                i18n_create_checkbox(hwnd, S_WARN, cfg->gauge_warn_enabled,
                              x1, y, 55, 20, IDC_WARN_CHECK, hFont);
                snprintf(buf, sizeof(buf), "%.1f", cfg->gauge_warn);
                CreateEdit(hwnd, buf, 70, y, editW, 22, IDC_WARN_EDIT, hFont);
                y += 28;

                i18n_create_checkbox(hwnd, S_CRIT, cfg->gauge_crit_enabled,
                              x1, y, 55, 20, IDC_CRIT_CHECK, hFont);
                snprintf(buf, sizeof(buf), "%.1f", cfg->gauge_crit);
                CreateEdit(hwnd, buf, 70, y, editW, 22, IDC_CRIT_EDIT, hFont);
                y += 30;
            }

            /* Trend-specific */
            if (isTrend) {
                i18n_create_label(hwnd, S_TREND_SETTINGS, x1, y, 200, 18, hFont);
                y += 25;
                i18n_create_label(hwnd, S_HOURS, x1, y + 3, 130, 18, hFont);
                snprintf(buf, sizeof(buf), "%d", cfg->trend_hours);
                CreateEdit(hwnd, buf, 145, y, editW, 22, IDC_HOURS_EDIT, hFont);
                y += 30;
            }

            /* Buttons */
            int btnY = y + 10;
            int winW = 300;
            i18n_create_button(hwnd, S_BTN_OK, BS_DEFPUSHBUTTON,
                               winW / 2 - 90, btnY, 85, 30, IDC_OK, hFont);
            i18n_create_button(hwnd, S_CANCEL, 0,
                               winW / 2 + 5, btnY, 85, 30, IDC_CANCEL, hFont);

            /* Size window to content */
            int winH = btnY + 48;
            RECT rc = {0, 0, winW, winH};
            AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

            /* Enumerate children and set font */
            EnumChildWindows(hwnd, SetChildFontProc, (LPARAM)hFont);

            /* Show opacity value */
            snprintf(buf, sizeof(buf), "%d", cfg->bg_opacity);
            SetWindowTextA(sd->hOpacityLabel, buf);
            break;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == GetDlgItem(hwnd, IDC_OPACITY_TRACK)) {
                int pos = (int)SendMessageA(GetDlgItem(hwnd, IDC_OPACITY_TRACK), TBM_GETPOS, 0, 0);
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", pos);
                SetWindowTextA(sd->hOpacityLabel, buf);
            }
            break;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
            if (dis->CtlID == IDC_COLOR_BUTTON) {
                DrawColorSwatch(dis->hwndItem, dis->hDC, sd->accent_color);
            }
            return TRUE;
        }

        case WM_COMMAND: {
            WORD cmd = LOWORD(wParam);
            if (cmd == IDC_COLOR_BUTTON) {
                /* Choose color */
                CHOOSECOLORA cc;
                memset(&cc, 0, sizeof(cc));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd;
                cc.rgbResult = sd->accent_color;
                COLORREF cust[16] = {0};
                cc.lpCustColors = cust;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorA(&cc)) {
                    sd->accent_color = cc.rgbResult;
                    InvalidateRect(GetDlgItem(hwnd, IDC_COLOR_BUTTON), NULL, TRUE);
                }
            } else if (cmd == IDC_OK) {
                WidgetConfig *cfg = &sd->wd->config;
                char buf[64];

                GetDlgItemTextA(hwnd, IDC_REFRESH_EDIT, buf, sizeof(buf));
                /* The worker thread reads wd->config without the lock, so guard
                   these writes with the same critical section to avoid a torn
                   read (e.g. of the 32-byte item_id). */
                EnterCriticalSection(&sd->wd->cs);
                cfg->refresh_interval = atoi(buf);
                if (cfg->refresh_interval < 5) cfg->refresh_interval = 5;

                cfg->always_on_top = (IsDlgButtonChecked(hwnd, IDC_TOPMOST_CHECK) == BST_CHECKED);

                cfg->bg_opacity = (int)SendMessageA(GetDlgItem(hwnd, IDC_OPACITY_TRACK), TBM_GETPOS, 0, 0);
                cfg->accent_color = sd->accent_color;

                if (cfg->type == WIDGET_GAUGE) {
                    GetDlgItemTextA(hwnd, IDC_MIN_EDIT, buf, sizeof(buf));
                    cfg->gauge_min = atof(buf);
                    GetDlgItemTextA(hwnd, IDC_MAX_EDIT, buf, sizeof(buf));
                    cfg->gauge_max = atof(buf);
                    cfg->gauge_warn_enabled = (IsDlgButtonChecked(hwnd, IDC_WARN_CHECK) == BST_CHECKED);
                    GetDlgItemTextA(hwnd, IDC_WARN_EDIT, buf, sizeof(buf));
                    cfg->gauge_warn = atof(buf);
                    cfg->gauge_crit_enabled = (IsDlgButtonChecked(hwnd, IDC_CRIT_CHECK) == BST_CHECKED);
                    GetDlgItemTextA(hwnd, IDC_CRIT_EDIT, buf, sizeof(buf));
                    cfg->gauge_crit = atof(buf);
                }

                if (cfg->type == WIDGET_TREND) {
                    GetDlgItemTextA(hwnd, IDC_HOURS_EDIT, buf, sizeof(buf));
                    cfg->trend_hours = atoi(buf);
                    if (cfg->trend_hours < 1) cfg->trend_hours = 1;
                }
                LeaveCriticalSection(&sd->wd->cs);

                /* Update topmost */
                widget_set_topmost(sd->wd->hwnd, cfg->always_on_top);

                /* Save config */
                sync_and_save(sd->wd);

                /* Re-render */
                PostMessage(sd->wd->hwnd, WM_REFRESH_WIDGET, 0, 0);

                DestroyWindow(hwnd);
            } else if (cmd == IDC_CANCEL) {
                DestroyWindow(hwnd);
            } else if (cmd == IDC_CHANGE_ITEM) {
                WidgetConfig new_wc;
                memset(&new_wc, 0, sizeof(new_wc));
                if (ui_select_show(GetModuleHandle(NULL), hwnd, sd->wd->api, &new_wc)) {
                    EnterCriticalSection(&sd->wd->cs);
                    strncpy(sd->wd->config.item_id, new_wc.item_id, sizeof(sd->wd->config.item_id) - 1);
                    strncpy(sd->wd->config.host_name, new_wc.host_name, sizeof(sd->wd->config.host_name) - 1);
                    strncpy(sd->wd->config.item_name, new_wc.item_name, sizeof(sd->wd->config.item_name) - 1);
                    strncpy(sd->wd->config.units, new_wc.units, sizeof(sd->wd->config.units) - 1);
                    sd->wd->config.value_type = new_wc.value_type;
                    LeaveCriticalSection(&sd->wd->cs);

                    /* Update display label */
                    {
                        char buf2[512];
                        snprintf(buf2, sizeof(buf2), "%s / %s", new_wc.host_name, new_wc.item_name);
                        set_text_utf8(sd->hCurrLabel, buf2);
                    }

                    /* Save config and refresh widget */
                    sync_and_save(sd->wd);
                    PostMessage(sd->wd->hwnd, WM_REFRESH_WIDGET, 0, 0);
                }
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;

        /* ---- Dark-theme control coloring ---- */

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, SET_CLR_TEXT);
            SetBkMode(hdcStatic, TRANSPARENT);
            SetBkColor(hdcStatic, SET_CLR_BG);
            return (LRESULT)sd->hBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, SET_CLR_TEXT);
            SetBkColor(hdcEdit, SET_CLR_EDIT_BG);
            return (LRESULT)sd->hEditBrush;
        }

        case WM_CTLCOLORBTN: {
            HDC hdcBtn = (HDC)wParam;
            SetTextColor(hdcBtn, SET_CLR_TEXT);
            SetBkColor(hdcBtn, SET_CLR_BG);
            return (LRESULT)sd->hBgBrush;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hOld = (HBRUSH)SelectObject(hdc, sd->hBgBrush);
            Rectangle(hdc, rc.left - 1, rc.top - 1, rc.right + 1, rc.bottom + 1);
            SelectObject(hdc, hOld);
            return 1;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Header bg */
            RECT hr = rc;
            hr.bottom = SET_HEADER_Y;
            HBRUSH hOld = (HBRUSH)SelectObject(hdc, sd->hHeaderBrush);
            Rectangle(hdc, hr.left - 1, hr.top - 1, hr.right + 1, hr.bottom);
            SelectObject(hdc, hOld);

            /* Separator */
            HPEN hSepPen = CreatePen(PS_SOLID, 1, SET_CLR_SEP);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hSepPen);
            MoveToEx(hdc, hr.left, hr.bottom - 1, NULL);
            LineTo(hdc, hr.right, hr.bottom - 1);
            SelectObject(hdc, hOldPen);
            DeleteObject(hSepPen);

            /* Title */
            HFONT hOldFont = (HFONT)SelectObject(hdc, sd->hTitleFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, SET_CLR_TEXT);

            RECT tr = hr;
            tr.top += 24;
            tr.left += SET_MARGIN;
            tr.bottom -= 6;

            wchar_t *titleTxt = utf8_to_wide(i18n_str(S_WIDGET_SETTINGS_TITLE));
            if (titleTxt) {
                DrawTextW(hdc, titleTxt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                free(titleTxt);
            }
            SelectObject(hdc, hOldFont);

            /* Accent underline */
            HPEN hAcc = CreatePen(PS_SOLID, 3, SET_CLR_ACCENT);
            hOldPen = (HPEN)SelectObject(hdc, hAcc);
            MoveToEx(hdc, SET_MARGIN, tr.bottom - 2, NULL);
            LineTo(hdc, SET_MARGIN + 40, tr.bottom - 2);
            SelectObject(hdc, hOldPen);
            DeleteObject(hAcc);

            /* Thin separator below "Current Monitor Item" area (y = SET_HEADER_Y + 73 = 155) */
            HPEN hSep = CreatePen(PS_SOLID, 1, SET_CLR_SEP);
            hOldPen = (HPEN)SelectObject(hdc, hSep);
            MoveToEx(hdc, SET_MARGIN, 155, NULL);
            LineTo(hdc, rc.right - SET_MARGIN, 155);
            SelectObject(hdc, hOldPen);
            DeleteObject(hSep);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (sd->hBgBrush)     { DeleteObject(sd->hBgBrush);     sd->hBgBrush = NULL; }
            if (sd->hEditBrush)   { DeleteObject(sd->hEditBrush);   sd->hEditBrush = NULL; }
            if (sd->hHeaderBrush) { DeleteObject(sd->hHeaderBrush); sd->hHeaderBrush = NULL; }
            if (sd->hTitleFont)   { DeleteObject(sd->hTitleFont);   sd->hTitleFont = NULL; }
            break;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

static void show_settings_dialog(HWND parent, WidgetData *wd)
{
    /* Register settings class if needed */
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hIcon = LoadIconA(GetModuleHandle(NULL), MAKEINTRESOURCEA(IDI_APP_ICON));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)CreateSolidBrush(SET_CLR_BG);
        wc.lpszClassName = "ZabbixSettingsDlg";
        RegisterClassExA(&wc);
        registered = 1;
    }

    SettingsData sd;
    memset(&sd, 0, sizeof(sd));
    sd.wd = wd;

    /* Use DialogBoxIndirectParam-like approach with CreateWindow */
    /* Actually, let's use a simple modal loop */
    HWND hDlg = CreateWindowExA(0, "ZabbixSettingsDlg", "",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 350,
        parent, NULL, GetModuleHandle(NULL), &sd);

    if (!hDlg) return;
    i18n_set_window_title(hDlg, S_WIDGET_SETTINGS_TITLE);

    /* Center on parent's monitor (multi-monitor aware) */
    {
        RECT rcParent;
        GetWindowRect(parent, &rcParent);
        POINT center = { rcParent.left + (rcParent.right - rcParent.left) / 2,
                         rcParent.top + (rcParent.bottom - rcParent.top) / 2 };
        HMONITOR hMon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        RECT wa;
        if (GetMonitorInfoA(hMon, &mi)) wa = mi.rcWork;
        else SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        RECT rcDlg;
        GetWindowRect(hDlg, &rcDlg);
        int x = wa.left + (wa.right - wa.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = wa.top + (wa.bottom - wa.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    /* Modal message loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessageA(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
}

/* ============ WIDGET WINDOW PROCEDURE ============ */

static LRESULT CALLBACK WidgetProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WidgetData *wd = (WidgetData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            wd = (WidgetData *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)wd);
            wd->hwnd = hwnd;

            InitializeCriticalSection(&wd->cs);
            wd->refresh_event = CreateEventA(NULL, TRUE, FALSE, NULL);

            /* Click-through when position is locked */
            if (wd->config.lock_position) {
                LONG ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
                SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
            }

            /* Initial paint */
            widget_paint(wd);

            /* Start worker thread */
            wd->running = 1;
            wd->thread = CreateThread(NULL, 0, widget_worker, wd, 0, NULL);
            return 0;
        }

        case WM_DATA_UPDATED: {
            widget_paint(wd);
            return 0;
        }

        case WM_REFRESH_WIDGET: {
            widget_paint(wd);
            return 0;
        }

        case WM_NCHITTEST: {
            /* Allow clicking through transparent areas */
            if (wd && wd->bits) {
                POINT pt;
                pt.x = GET_X_LPARAM(lParam);
                pt.y = GET_Y_LPARAM(lParam);
                ScreenToClient(hwnd, &pt);
                if (pt.x >= 0 && pt.x < wd->width && pt.y >= 0 && pt.y < wd->height) {
                    unsigned char *pixel = (unsigned char *)wd->bits;
                    pixel += (pt.y * wd->width + pt.x) * 4;
                    if (pixel[3] > 20) /* alpha threshold */
                        return HTCLIENT;
                }
            }
            return HTTRANSPARENT;
        }

        case WM_MOUSEACTIVATE:
            /* Never take activation: prevents the desktop icon layer from being
             * pushed above the widget when it is pinned to the desktop. */
            return MA_NOACTIVATE;

        case WM_LBUTTONDOWN: {
            /* Locked widgets are click-through; ignore mouse entirely */
            if (wd && wd->config.lock_position)
                return 0;
            /* Start dragging - track with global cursor coords so it works
             * both as a top-level window and as a child of the desktop host */
            wd->dragging = 1;
            SetCapture(hwnd);
            GetCursorPos(&wd->drag_start);
            RECT rc;
            GetWindowRect(hwnd, &rc); /* screen coords in all cases */
            wd->drag_origin.x = rc.left;
            wd->drag_origin.y = rc.top;
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (wd && wd->dragging) {
                POINT cur;
                GetCursorPos(&cur);
                int nx = wd->drag_origin.x + (cur.x - wd->drag_start.x);
                int ny = wd->drag_origin.y + (cur.y - wd->drag_start.y);
                move_widget_to_screen_pos(hwnd, nx, ny);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (wd && wd->dragging) {
                wd->dragging = 0;
                ReleaseCapture();
                /* Save position */
                RECT rc;
                GetWindowRect(hwnd, &rc);
                wd->config.x = rc.left;
                wd->config.y = rc.top;
                sync_and_save(wd);
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ClientToScreen(hwnd, &pt);
            show_context_menu(hwnd, wd, pt.x, pt.y);
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_TOGGLE_TOPMOST: {
                    wd->config.always_on_top = !wd->config.always_on_top;
                    widget_set_topmost(hwnd, wd->config.always_on_top);
                    sync_and_save(wd);
                    break;
                }
                case IDM_LOCK_POSITION: {
                    wd->config.lock_position = !wd->config.lock_position;
                    widget_set_locked(hwnd, wd->config.lock_position);
                    sync_and_save(wd);
                    break;
                }
                case IDM_CONFIGURE: {
                    show_settings_dialog(hwnd, wd);
                    break;
                }
                case IDM_REFRESH_NOW: {
                    /* Signal worker thread to wake up and fetch data immediately */
                    if (wd->refresh_event)
                        SetEvent(wd->refresh_event);
                    else
                        PostMessage(hwnd, WM_DATA_UPDATED, 0, 0);
                    break;
                }
                case IDM_REMOVE: {
                    /* Notify main window before destroying */
                    if (wd->hMainWnd)
                        SendMessageA(wd->hMainWnd, WM_WIDGET_REMOVED,
                                     wd->config_index, (LPARAM)hwnd);
                    /* Remove from config and destroy */
                    if (wd->app_config) {
                        config_remove_widget(wd->app_config, wd->config_index);
                        char path[MAX_PATH];
                        config_get_default_path(path, sizeof(path));
                        config_save(wd->app_config, path);
                    }
                    DestroyWindow(hwnd);
                    break;
                }
            }
            return 0;
        }

        case WM_DESTROY: {
            /* Stop worker thread.
             * wd (and its render buffers) are freed by the worker thread on
             * exit, so after WaitForSingleObject returns we must NOT dereference
             * wd — doing so would be a use-after-free. Snapshot the thread
             * handle into a local first and only touch that local afterwards. */
            HANDLE thread = NULL;
            if (wd && wd->thread) {
                thread = wd->thread;
                wd->running = 0;
                if (wd->refresh_event) SetEvent(wd->refresh_event); /* wake from sleep loop */
                if (wd->api) zabbix_api_abort(wd->api);            /* interrupt in-flight HTTP */
            }
            if (thread) {
                WaitForSingleObject(thread, 3000);
                CloseHandle(thread);
            }
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            return 0;
        }

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* ============ PUBLIC API ============ */

static const char *WIDGET_CLASS = "ZabbixWidgetClass";

int widget_register_class(HINSTANCE hInstance)
{
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WidgetProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = WIDGET_CLASS;
    return RegisterClassExA(&wc) ? 0 : -1;
}

HWND widget_create(HINSTANCE hInstance, ZabbixAPI *api,
                   AppConfig *config, int config_index,
                   int x, int y, HWND hMainWnd)
{
    if (!config || config_index < 0 || config_index >= config->widget_count)
        return NULL;

    WidgetConfig *wc = &config->widgets[config_index];

    WidgetData *wd = (WidgetData *)calloc(1, sizeof(WidgetData));
    if (!wd) return NULL;

    wd->config = *wc;
    wd->api = api;
    wd->app_config = config;
    wd->config_index = config_index;
    wd->hMainWnd = hMainWnd;

    DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    /* Pin mode (topmost vs desktop) is applied by widget_apply_pin() after creation */

    /* If position is (0,0) or not visible on any monitor, center on cursor's monitor */
    if ((x == 0 && y == 0) || !is_position_visible(x, y, wc->width, wc->height)) {
        center_on_cursor_monitor(&x, &y, wc->width, wc->height);
    }

    HWND hwnd = CreateWindowExA(exStyle, WIDGET_CLASS, "ZabbixWidget",
        WS_POPUP,
        x, y, wc->width, wc->height,
        NULL, NULL, hInstance, wd);

    if (!hwnd) {
        free(wd);
        return NULL;
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    /* Apply pin mode AFTER the window is fully created and shown:
     * topmost -> floating TOPMOST window; otherwise -> pinned to desktop
     * (child of WorkerW/Progman, immune to Win+D). */
    widget_apply_pin(hwnd, wc->always_on_top);
    return hwnd;
}

void widget_refresh(HWND hwnd)
{
    PostMessage(hwnd, WM_DATA_UPDATED, 0, 0);
}

void widget_update_config(HWND hwnd, const WidgetConfig *new_cfg)
{
    WidgetData *wd = (WidgetData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!wd) return;
    EnterCriticalSection(&wd->cs);
    wd->config = *new_cfg;
    LeaveCriticalSection(&wd->cs);
    PostMessage(hwnd, WM_REFRESH_WIDGET, 0, 0);
}

int widget_get_config_index(HWND hwnd)
{
    WidgetData *wd = (WidgetData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    return wd ? wd->config_index : -1;
}

void widget_set_topmost(HWND hwnd, int topmost)
{
    /* topmost  -> floating top-level TOPMOST window (covers normal windows)
     * !topmost -> pinned onto the desktop (survives Win+D, sits above icons) */
    widget_apply_pin(hwnd, topmost);
}

void widget_set_locked(HWND hwnd, int locked)
{
    WidgetData *wd = (WidgetData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    LONG ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (locked)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
    if (wd) {
        wd->config.lock_position = locked;
        sync_and_save(wd);
    }
}

void widget_set_config_index(HWND hwnd, int index)
{
    WidgetData *wd = (WidgetData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (wd) wd->config_index = index;
}
