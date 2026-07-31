#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "ui_login.h"
#include "zabbix_api.h"
#include "config.h"
#include "i18n.h"
#include "resource.h"

#define IDC_URL_EDIT     4001
#define IDC_USER_EDIT    4002
#define IDC_PASS_EDIT    4003
#define IDC_LOGIN_BTN    4004
#define IDC_CANCEL_BTN   4005
#define IDC_STATUS_LABEL 4006
#define IDC_URL_HINT     4007

/* ---- Dark theme colours ---- */
#define CLR_BG        RGB( 30,  33,  48)
#define CLR_HEADER_BG RGB( 24,  27,  40)
#define CLR_TEXT      RGB(228, 228, 240)
#define CLR_HINT      RGB(130, 135, 155)
#define CLR_ACCENT    RGB( 74, 144, 217)
#define CLR_EDIT_BG   RGB( 22,  24,  36)
#define CLR_BTN_BG    RGB( 50,  54,  72)
#define CLR_SEP       RGB( 60,  64,  84)

/* Layout constants */
#define WIN_W         480
#define WIN_H         370
#define MARGIN_X       36
#define CONTENT_Y      95
#define EDIT_W        408
#define EDIT_H         30
#define BTN_H          34
#define BTN_W          96

typedef struct {
    AppConfig *config;
    ZabbixAPI *api;
    HWND hUrl, hUser, hPass, hStatus, hHint;
    HFONT  hTitleFont;
    HBRUSH hBgBrush;
    HBRUSH hEditBrush;
    HBRUSH hHeaderBrush;
    int    result;
} LoginData;

static HFONT GetFont(void)
{
    NONCLIENTMETRICSA ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        return CreateFontIndirectA(&ncm.lfMenuFont);
    return CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

static HFONT MakeTitleFont(void)
{
    return CreateFontA(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

static HFONT GetEditFont(void)
{
    return CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

/* ---------- Custom drawing helpers ---------- */

static void draw_header(HDC hdc, RECT *rc, LoginData *ld)
{
    RECT hr = *rc;
    hr.bottom = 82;

    /* Header background */
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, ld->hHeaderBrush);
    Rectangle(hdc, hr.left - 1, hr.top - 1, hr.right + 1, hr.bottom);
    SelectObject(hdc, hOld);

    /* Separator line */
    HPEN hSepPen = CreatePen(PS_SOLID, 1, CLR_SEP);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hSepPen);
    MoveToEx(hdc, hr.left, hr.bottom - 1, NULL);
    LineTo(hdc, hr.right, hr.bottom - 1);
    SelectObject(hdc, hOldPen);
    DeleteObject(hSepPen);

    /* Title text */
    HFONT hOldFont = (HFONT)SelectObject(hdc, ld->hTitleFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT);

    RECT tr = hr;
    tr.top += 24;
    tr.left += MARGIN_X;
    tr.right -= MARGIN_X;
    tr.bottom -= 8;

    {
        wchar_t *wtitle = utf8_to_wide(i18n_str(S_LOGIN_TITLE));
        if (wtitle) {
            DrawTextW(hdc, wtitle, -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            free(wtitle);
        }
    }

    SelectObject(hdc, hOldFont);

    /* Accent underline */
    HPEN hAccentPen = CreatePen(PS_SOLID, 3, CLR_ACCENT);
    hOldPen = (HPEN)SelectObject(hdc, hAccentPen);
    MoveToEx(hdc, MARGIN_X, tr.bottom - 2, NULL);
    LineTo(hdc, MARGIN_X + 48, tr.bottom - 2);
    SelectObject(hdc, hOldPen);
    DeleteObject(hAccentPen);
}

/* ---------- Window procedure ---------- */

static LRESULT CALLBACK LoginProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LoginData *ld = (LoginData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            ld = (LoginData *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)ld);

            /* Brushes */
            ld->hBgBrush     = CreateSolidBrush(CLR_BG);
            ld->hEditBrush   = CreateSolidBrush(CLR_EDIT_BG);
            ld->hHeaderBrush = CreateSolidBrush(CLR_HEADER_BG);
            ld->hTitleFont   = MakeTitleFont();

            HFONT hFont = GetFont();
            HFONT eFont = GetEditFont();

            int x = MARGIN_X;
            int y = CONTENT_Y;

            /* --- URL field --- */
            i18n_create_label(hwnd, S_ZABBIX_URL, x, y, EDIT_W, 18, hFont);
            y += 20;
            ld->hUrl = i18n_create_edit(hwnd, "", x, y, EDIT_W, EDIT_H, IDC_URL_EDIT, eFont);
            y += EDIT_H + 2;
            ld->hHint = i18n_create_label(hwnd, S_HTTP_HTTPS, x + 2, y, EDIT_W, 15, hFont);
            y += 25;

            /* --- Username field --- */
            i18n_create_label(hwnd, S_USERNAME, x, y, EDIT_W, 18, hFont);
            y += 20;
            ld->hUser = i18n_create_edit(hwnd, "", x, y, EDIT_W, EDIT_H, IDC_USER_EDIT, eFont);
            y += EDIT_H + 10;

            /* --- Password field --- */
            i18n_create_label(hwnd, S_PASSWORD, x, y, EDIT_W, 18, hFont);
            y += 20;
            ld->hPass = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                x, y, EDIT_W, EDIT_H,
                hwnd, (HMENU)(INT_PTR)IDC_PASS_EDIT, NULL, NULL);
            SendMessageW(ld->hPass, WM_SETFONT, (WPARAM)eFont, TRUE);
            y += EDIT_H + 8;

            /* --- Status text --- */
            ld->hStatus = i18n_create_label(hwnd, S_LOADING, x, y, EDIT_W, 20, hFont);

            /* --- Buttons (right-aligned) --- */
            int btnY = WIN_H - BTN_H - 18;
            i18n_create_button(hwnd, S_CANCEL, BS_PUSHBUTTON,
                               WIN_W - MARGIN_X - BTN_W - 104, btnY, BTN_W, BTN_H,
                               IDC_CANCEL_BTN, hFont);
            i18n_create_button(hwnd, S_LOGIN, BS_DEFPUSHBUTTON,
                               WIN_W - MARGIN_X - BTN_W, btnY, BTN_W, BTN_H,
                               IDC_LOGIN_BTN, hFont);

            i18n_set_window_title(hwnd, S_LOGIN_TITLE);

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
            RECT rc = {0, 0, WIN_W, WIN_H};
            AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOZORDER);

            /* Set tab order: URL -> User -> Pass -> Login -> Cancel */
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
                        set_text_utf8(ld->hStatus, i18n_str(S_FILL_URL_USER));
                        return 0;
                    }

                    set_text_utf8(ld->hStatus, i18n_str(S_CONNECTING));
                    UpdateWindow(ld->hStatus);

                    int ret = zabbix_api_login(ld->api, url, user, pass);
                    if (ret == 0) {
                        strncpy(ld->config->zabbix_url, url, sizeof(ld->config->zabbix_url) - 1);
                        strncpy(ld->config->zabbix_user, user, sizeof(ld->config->zabbix_user) - 1);
                        strncpy(ld->config->zabbix_pass, pass, sizeof(ld->config->zabbix_pass) - 1);

                        char path[MAX_PATH];
                        config_get_default_path(path, sizeof(path));
                        config_save(ld->config, path);

                        ld->result = 1;
                        DestroyWindow(hwnd);
                    } else {
                        i18n_set_text_fmt(ld->hStatus, i18n_str(S_LOGIN_FAILED_FMT),
                                         zabbix_api_error());
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

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND ctrl = (HWND)lParam;
            if (ctrl == ld->hHint || ctrl == ld->hStatus)
                SetTextColor(hdcStatic, CLR_HINT);
            else
                SetTextColor(hdcStatic, CLR_TEXT);
            SetBkMode(hdcStatic, TRANSPARENT);
            SetBkColor(hdcStatic, CLR_BG);
            return (LRESULT)ld->hBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, CLR_TEXT);
            SetBkColor(hdcEdit, CLR_EDIT_BG);
            return (LRESULT)ld->hEditBrush;
        }

        case WM_CTLCOLORBTN: {
            HDC hdcBtn = (HDC)wParam;
            SetTextColor(hdcBtn, CLR_TEXT);
            SetBkColor(hdcBtn, CLR_BTN_BG);
            return (LRESULT)GetStockObject(DC_BRUSH);
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Background */
            HBRUSH hOld = (HBRUSH)SelectObject(hdc, ld->hBgBrush);
            Rectangle(hdc, rc.left - 1, rc.top - 1, rc.right + 1, rc.bottom + 1);
            SelectObject(hdc, hOld);

            return 1;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);
            draw_header(hdc, &rc, ld);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            ld->result = 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (ld->hBgBrush)     { DeleteObject(ld->hBgBrush); ld->hBgBrush = NULL; }
            if (ld->hEditBrush)   { DeleteObject(ld->hEditBrush); ld->hEditBrush = NULL; }
            if (ld->hHeaderBrush) { DeleteObject(ld->hHeaderBrush); ld->hHeaderBrush = NULL; }
            if (ld->hTitleFont)   { DeleteObject(ld->hTitleFont); ld->hTitleFont = NULL; }
            break;

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
        wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
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

    HWND hwnd = CreateWindowExA(0, "ZabbixLoginDlg", "",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
        parent, NULL, hInstance, &ld);

    if (!hwnd) return 0;
    i18n_set_window_title(hwnd, S_LOGIN_TITLE);

    /* Center on cursor's monitor */
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
