#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "ui_login.h"
#include "zabbix_api.h"
#include "config.h"

#define IDC_URL_EDIT     4001
#define IDC_USER_EDIT    4002
#define IDC_PASS_EDIT    4003
#define IDC_LOGIN_BTN    4004
#define IDC_CANCEL_BTN   4005
#define IDC_STATUS_LABEL 4006

typedef struct {
    AppConfig *config;
    ZabbixAPI *api;
    HWND hUrl, hUser, hPass, hStatus;
    int result;
} LoginData;

static HFONT GetFont(void)
{
    return CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

static LRESULT CALLBACK LoginProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LoginData *ld = (LoginData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            ld = (LoginData *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ld);

            HFONT hFont = GetFont();

            /* Labels */
            HWND hLbl;
            hLbl = CreateWindowExA(0, "STATIC", "Zabbix URL:", WS_CHILD | WS_VISIBLE,
                15, 15, 100, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            hLbl = CreateWindowExA(0, "STATIC", "(http/https)", WS_CHILD | WS_VISIBLE,
                15, 32, 100, 14, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            hLbl = CreateWindowExA(0, "STATIC", "Username:", WS_CHILD | WS_VISIBLE,
                15, 50, 100, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            hLbl = CreateWindowExA(0, "STATIC", "Password:", WS_CHILD | WS_VISIBLE,
                15, 85, 100, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Edit boxes */
            ld->hUrl = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                120, 12, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_URL_EDIT, NULL, NULL);
            SendMessageA(ld->hUrl, WM_SETFONT, (WPARAM)hFont, TRUE);

            ld->hUser = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                120, 47, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_USER_EDIT, NULL, NULL);
            SendMessageA(ld->hUser, WM_SETFONT, (WPARAM)hFont, TRUE);

            ld->hPass = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                120, 82, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_PASS_EDIT, NULL, NULL);
            SendMessageA(ld->hPass, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Status label */
            ld->hStatus = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE,
                15, 120, 400, 20, hwnd, (HMENU)(INT_PTR)IDC_STATUS_LABEL, NULL, NULL);
            SendMessageA(ld->hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Buttons */
            CreateWindowExA(0, "BUTTON", "Login", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                220, 155, 90, 30, hwnd, (HMENU)(INT_PTR)IDC_LOGIN_BTN, NULL, NULL);
            SendMessageA(GetDlgItem(hwnd, IDC_LOGIN_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);

            CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
                320, 155, 90, 30, hwnd, (HMENU)(INT_PTR)IDC_CANCEL_BTN, NULL, NULL);
            SendMessageA(GetDlgItem(hwnd, IDC_CANCEL_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Pre-fill from config */
            if (ld->config->zabbix_url[0])
                SetWindowTextA(ld->hUrl, ld->config->zabbix_url);
            else
                SetWindowTextA(ld->hUrl, "http://localhost/zabbix");
            if (ld->config->zabbix_user[0])
                SetWindowTextA(ld->hUser, ld->config->zabbix_user);
            else
                SetWindowTextA(ld->hUser, "Admin");
            if (ld->config->zabbix_pass[0])
                SetWindowTextA(ld->hPass, ld->config->zabbix_pass);

            /* Size window */
            RECT rc = {0, 0, 440, 200};
            AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

            /* Set focus to URL */
            SetFocus(ld->hUrl);
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_LOGIN_BTN: {
                    char url[512], user[128], pass[128];
                    GetWindowTextA(ld->hUrl, url, sizeof(url));
                    GetWindowTextA(ld->hUser, user, sizeof(user));
                    GetWindowTextA(ld->hPass, pass, sizeof(pass));

                    if (!url[0] || !user[0]) {
                        SetWindowTextA(ld->hStatus, "Please fill in URL and username");
                        return 0;
                    }

                    SetWindowTextA(ld->hStatus, "Connecting...");
                    UpdateWindow(ld->hStatus);

                    int ret = zabbix_api_login(ld->api, url, user, pass);
                    if (ret == 0) {
                        /* Save to config */
                        strncpy(ld->config->zabbix_url, url, sizeof(ld->config->zabbix_url) - 1);
                        strncpy(ld->config->zabbix_user, user, sizeof(ld->config->zabbix_user) - 1);
                        strncpy(ld->config->zabbix_pass, pass, sizeof(ld->config->zabbix_pass) - 1);

                        /* Save config file */
                        char path[MAX_PATH];
                        config_get_default_path(path, sizeof(path));
                        config_save(ld->config, path);

                        ld->result = 1;
                        DestroyWindow(hwnd);
                    } else {
                        char errmsg[600];
                        snprintf(errmsg, sizeof(errmsg), "Login failed: %s", zabbix_api_error());
                        SetWindowTextA(ld->hStatus, errmsg);
                    }
                    break;
                }
                case IDC_CANCEL_BTN:
                    ld->result = 0;
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }

        case WM_CLOSE:
            ld->result = 0;
            DestroyWindow(hwnd);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int ui_login_show(HINSTANCE hInstance, HWND parent, AppConfig *config, ZabbixAPI *api)
{
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = LoginProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "ZabbixLoginDlg";
        RegisterClassExA(&wc);
        registered = 1;
    }

    LoginData ld;
    memset(&ld, 0, sizeof(ld));
    ld.config = config;
    ld.api = api;
    ld.result = 0;

    HWND hwnd = CreateWindowExA(0, "ZabbixLoginDlg", "Zabbix Login",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 440, 200,
        parent, NULL, hInstance, &ld);

    if (!hwnd) return 0;

    /* Center on cursor's monitor (multi-monitor aware) */
    {
        POINT cursor;
        GetCursorPos(&cursor);
        HMONITOR hMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        RECT wa;
        if (GetMonitorInfoA(hMon, &mi)) wa = mi.rcWork;
        else SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        RECT rcDlg;
        GetWindowRect(hwnd, &rcDlg);
        int x = wa.left + (wa.right - wa.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = wa.top + (wa.bottom - wa.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* Modal loop */
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsWindow(hwnd)) break;
        if (!IsDialogMessageA(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return ld.result;
}
