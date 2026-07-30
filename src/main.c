#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "zabbix_api.h"
#include "render.h"
#include "widget.h"
#include "ui_login.h"
#include "ui_select.h"
#include "i18n.h"
#include "resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON       (WM_USER + 300)
#define IDM_TRAY_ADD      7001
#define IDM_TRAY_LOGIN    7002
#define IDM_TRAY_REFRESH  7003
#define IDM_TRAY_UNLOCK_ALL 7005
#define IDM_TRAY_EXIT     7004

#define MAX_WIDGET_WNDS 64
#define TRAY_ICON_ID    1

static AppConfig g_config;
static ZabbixAPI g_api;
static HWND g_widget_hwnds[MAX_WIDGET_WNDS];
static int g_widget_hwnd_count = 0;
static HWND g_hMainWnd = NULL;
static HINSTANCE g_hInstance = NULL;
static NOTIFYICONDATAA g_nid;
static int g_exiting = 0;
static HANDLE g_hSingleInstanceMutex = NULL;

/* Find and remove a widget HWND from the array */
static void remove_widget_hwnd(HWND hwnd)
{
    for (int i = 0; i < g_widget_hwnd_count; i++) {
        if (g_widget_hwnds[i] == hwnd) {
            for (int j = i; j < g_widget_hwnd_count - 1; j++)
                g_widget_hwnds[j] = g_widget_hwnds[j + 1];
            g_widget_hwnd_count--;
            return;
        }
    }
}

/* Create a new widget from a WidgetConfig and track it */
static HWND create_and_track_widget(WidgetConfig *wc, int config_idx)
{
    /* Ensure config has the widget */
    HWND hw = widget_create(g_hInstance, &g_api, &g_config, config_idx,
                            wc->x, wc->y, g_hMainWnd);
    if (hw && g_widget_hwnd_count < MAX_WIDGET_WNDS) {
        g_widget_hwnds[g_widget_hwnd_count++] = hw;
    }
    return hw;
}

/* Show tray context menu */
static void show_tray_menu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    i18n_append_menu(hMenu, MF_STRING, IDM_TRAY_ADD, S_ADD_WIDGET_ELLIPSIS);
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    i18n_append_menu(hMenu, MF_STRING, IDM_TRAY_LOGIN, S_LOGIN_SETTINGS);
    i18n_append_menu(hMenu, MF_STRING, IDM_TRAY_REFRESH, S_REFRESH_ALL);
    i18n_append_menu(hMenu, MF_STRING, IDM_TRAY_UNLOCK_ALL, S_UNLOCK_ALL);
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    i18n_append_menu(hMenu, MF_STRING, IDM_TRAY_EXIT, S_EXIT);

    POINT pt;
    GetCursorPos(&pt);
    /* Set foreground window for menu to work properly */
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    PostMessageA(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

/* Main window procedure (hidden window for message handling) */
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE:
            g_hMainWnd = hwnd;
            return 0;

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP) {
                show_tray_menu(hwnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                /* Double-click: add widget */
                PostMessageA(hwnd, WM_COMMAND, IDM_TRAY_ADD, 0);
            }
            return 0;
        }

        case WM_WIDGET_REMOVED: {
            int removed_idx = (int)wParam;
            HWND removed_hwnd = (HWND)lParam;
            remove_widget_hwnd(removed_hwnd);
            /* Update config indices for remaining widgets */
            for (int i = 0; i < g_widget_hwnd_count; i++) {
                int idx = widget_get_config_index(g_widget_hwnds[i]);
                if (idx > removed_idx)
                    widget_set_config_index(g_widget_hwnds[i], idx - 1);
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_TRAY_ADD: {
                    if (!g_api.connected) {
                        i18n_message_box(hwnd, S_NOT_CONNECTED, S_ERROR, MB_ICONWARNING);
                        break;
                    }
                    WidgetConfig wc;
                    memset(&wc, 0, sizeof(wc));
                    if (ui_select_show(g_hInstance, hwnd, &g_api, &wc)) {
                        int idx = config_add_widget(&g_config, &wc);
                        if (idx >= 0) {
                            /* Save config */
                            char path[MAX_PATH];
                            config_get_default_path(path, sizeof(path));
                            config_save(&g_config, path);
                            /* Create widget */
                            create_and_track_widget(&g_config.widgets[idx], idx);
                        }
                    }
                    break;
                }

                case IDM_TRAY_LOGIN: {
                    if (ui_login_show(g_hInstance, hwnd, &g_config, &g_api)) {
                        /* Refresh all widgets */
                        for (int i = 0; i < g_widget_hwnd_count; i++)
                            widget_refresh(g_widget_hwnds[i]);
                    }
                    break;
                }

                case IDM_TRAY_REFRESH: {
                    for (int i = 0; i < g_widget_hwnd_count; i++)
                        widget_refresh(g_widget_hwnds[i]);
                    break;
                }

                case IDM_TRAY_UNLOCK_ALL: {
                    for (int i = 0; i < g_widget_hwnd_count; i++)
                        widget_set_locked(g_widget_hwnds[i], 0);
                    break;
                }

                case IDM_TRAY_EXIT: {
                    g_exiting = 1;
                    DestroyWindow(hwnd);
                    break;
                }
            }
            return 0;
        }

        case WM_QUERYENDSESSION:
        case WM_ENDSESSION: {
            g_exiting = 1;
            DestroyWindow(hwnd);
            return TRUE;
        }

        case WM_DESTROY: {
            /* Stop all widget threads */
            for (int i = 0; i < g_widget_hwnd_count; i++) {
                if (IsWindow(g_widget_hwnds[i]))
                    DestroyWindow(g_widget_hwnds[i]);
            }
            /* Remove tray icon */
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* Create system tray icon */
static int setup_tray_icon(HWND hwnd)
{
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(g_hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    {
        char *tip = utf8_to_acp(i18n_str(S_TRAY_TIP));
        if (tip) {
            strncpy(g_nid.szTip, tip, sizeof(g_nid.szTip) - 1);
            free(tip);
        }
    }

    return Shell_NotifyIconA(NIM_ADD, &g_nid) ? 0 : -1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmdLine, int showCmd)
{
    g_hInstance = hInstance;

    /* Initialize i18n (auto-detect OS language) */
    i18n_init();

    /* Single-instance guard: bail out if another instance is already running */
    g_hSingleInstanceMutex = CreateMutexA(NULL, FALSE,
        "ZabbixDesktopInfo_SingleInstanceMutex");
    if (!g_hSingleInstanceMutex) {
        i18n_message_box(NULL, S_FAILED_REGISTER_CLASS, S_ERROR, MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        i18n_message_box(NULL, S_ALREADY_RUNNING, S_TRAY_TIP, MB_ICONINFORMATION);
        CloseHandle(g_hSingleInstanceMutex);
        g_hSingleInstanceMutex = NULL;
        return 0;
    }

    /* Initialize common controls */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    /* Initialize GDI+ */
    if (render_init() != 0) {
        i18n_message_box(NULL, S_FAILED_INIT_GDIPLUS, S_ERROR, MB_ICONERROR);
        return 1;
    }

    /* Load config */
    config_init(&g_config);
    char config_path[MAX_PATH];
    config_get_default_path(config_path, sizeof(config_path));
    config_load(&g_config, config_path);

    /* Initialize Zabbix API */
    zabbix_api_init(&g_api);

    /* Register widget window class */
    widget_register_class(hInstance);

    /* Register main window class */
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
    wc.lpszClassName = "ZabbixDesktopMain";
    if (!RegisterClassExA(&wc)) {
        i18n_message_box(NULL, S_FAILED_REGISTER_CLASS, S_ERROR, MB_ICONERROR);
        return 1;
    }

    /* Create hidden main window */
    HWND hMain = CreateWindowExA(0, "ZabbixDesktopMain", "ZabbixDesktop",
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL);
    if (!hMain) {
        i18n_message_box(NULL, S_FAILED_CREATE_MAIN, S_ERROR, MB_ICONERROR);
        return 1;
    }

    /* Setup tray icon */
    setup_tray_icon(hMain);

    /* Try to auto-login if config has credentials */
    if (g_config.zabbix_url[0] && g_config.zabbix_user[0] && g_config.zabbix_pass[0]) {
        zabbix_api_login(&g_api, g_config.zabbix_url,
                         g_config.zabbix_user, g_config.zabbix_pass);
    }

    /* If not connected, show login dialog */
    if (!g_api.connected) {
        if (!ui_login_show(hInstance, hMain, &g_config, &g_api)) {
            /* User cancelled login - exit */
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            return 0;
        }
    }

    /* Create widgets from config */
    for (int i = 0; i < g_config.widget_count; i++) {
        HWND hw = widget_create(hInstance, &g_api, &g_config, i,
                                g_config.widgets[i].x, g_config.widgets[i].y, hMain);
        if (hw && g_widget_hwnd_count < MAX_WIDGET_WNDS)
            g_widget_hwnds[g_widget_hwnd_count++] = hw;
    }

    /* Show notification if no widgets */
    if (g_widget_hwnd_count == 0) {
        /* Could show a balloon tip, but for now just let the user
           right-click the tray icon to add widgets */
        g_nid.dwInfoFlags = NIIF_INFO;
        {
            char *info = utf8_to_acp(i18n_str(S_RIGHT_CLICK_ADD));
            char *title = utf8_to_acp(i18n_str(S_BALLOON_TITLE));
            if (info) { strncpy(g_nid.szInfo, info, sizeof(g_nid.szInfo) - 1); free(info); }
            if (title) { strncpy(g_nid.szInfoTitle, title, sizeof(g_nid.szInfoTitle) - 1); free(title); }
        }
        g_nid.uTimeout = 5000;
        g_nid.uFlags |= NIF_INFO;
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
        g_nid.uFlags &= ~NIF_INFO;
    }

    /* Message loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* Cleanup */
    /* Save config */
    config_save(&g_config, config_path);
    render_shutdown();

    if (g_hSingleInstanceMutex) {
        CloseHandle(g_hSingleInstanceMutex);
        g_hSingleInstanceMutex = NULL;
    }

    return (int)msg.wParam;
}
