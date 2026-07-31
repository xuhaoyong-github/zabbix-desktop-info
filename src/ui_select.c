#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "ui_select.h"
#include "zabbix_api.h"
#include "config.h"
#include "i18n.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

#define WM_HOSTS_LOADED  (WM_USER + 10)
#define WM_ITEMS_LOADED  (WM_USER + 11)

#define IDC_HOST_LIST     5001
#define IDC_ITEM_LIST     5002
#define IDC_ITEM_SEARCH   5003
#define IDC_HOST_SEARCH   5010
#define IDC_RADIO_GAUGE   5004
#define IDC_RADIO_CARD    5005
#define IDC_RADIO_TREND   5006
#define IDC_ADD_BTN       5007
#define IDC_CANCEL_BTN    5008
#define IDC_STATUS_LABEL  5009

/* ---- shadcn-inspired dark palette ---- */
#define CLR_BG         RGB(30,  33,  48)
#define CLR_HEADER_BG  RGB(24,  27,  40)
#define CLR_TEXT       RGB(228, 228, 240)
#define CLR_HINT       RGB(130, 135, 155)
#define CLR_ACCENT     RGB(74,  144, 217)
#define CLR_EDIT_BG    RGB(22,  24,  36)
#define CLR_LIST_BG    RGB(22,  24,  36)
#define CLR_BORDER     RGB(60,  64,  84)
#define CLR_SEP        RGB(60,  64,  84)

#define WIN_W  560
#define WIN_H  490
#define MARGIN 16
#define COL_GAP 14
#define HOST_W 180
#define ITEM_W (WIN_W - HOST_W - MARGIN * 3 - COL_GAP)

typedef struct {
    ZabbixAPI *api;
    HWND hwnd;
    int done;
    int error;
    /* Output */
    void *data;
    int count;
    char host_id_buf[64];
} FetchParams;

typedef struct {
    ZabbixAPI *api;
    HWND hHostList, hHostSearch, hItemList, hSearch, hStatus;
    HWND hRadioGauge, hRadioCard, hRadioTrend;
    int result;

    /* Fetched data */
    ZabbixHost *hosts;
    int host_count;
    ZabbixItem *items;
    int item_count;
    int current_host_idx;
    int items_loading;

    /* Selected item */
    ZabbixItem selected_item;
    int has_selection;

    /* Threads */
    HANDLE host_thread;
    HANDLE item_thread;
    FetchParams host_fetch;
    FetchParams item_fetch;

    /* Output */
    WidgetConfig *out_config;

    /* Dark theme resources */
    HFONT  hTitleFont;
    HBRUSH hBgBrush;
    HBRUSH hEditBrush;
    HBRUSH hHeaderBrush;
    HBRUSH hListBrush;
} SelectData;

static HFONT GetFont(void)
{
    NONCLIENTMETRICS ncm;
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
    return CreateFontA(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SWISS, "Segoe UI");
}

/* Thread: fetch hosts */
static DWORD WINAPI fetch_hosts_thread(LPVOID param)
{
    FetchParams *fp = (FetchParams *)param;
    ZabbixHost *hosts = NULL;
    int count = zabbix_api_get_hosts(fp->api, &hosts);
    fp->data = hosts;
    fp->count = count;
    fp->error = (count < 0);
    fp->done = 1;
    PostMessageA(fp->hwnd, WM_HOSTS_LOADED, 0, 0);
    return 0;
}

/* Thread: fetch items for a host */
static DWORD WINAPI fetch_items_thread(LPVOID param)
{
    FetchParams *fp = (FetchParams *)param;
    ZabbixItem *items = NULL;
    int count = zabbix_api_get_items(fp->api, fp->host_id_buf, &items);
    fp->data = items;
    fp->count = count;
    fp->error = (count < 0);
    fp->done = 1;
    PostMessageA(fp->hwnd, WM_ITEMS_LOADED, 0, 0);
    return 0;
}

static void fill_host_list(SelectData *sd, const wchar_t *wfilter)
{
    SendMessageW(sd->hHostList, LB_RESETCONTENT, 0, 0);
    wchar_t wfilter_lower[256] = {0};
    if (wfilter && *wfilter) {
        wcsncpy(wfilter_lower, wfilter, 255);
        CharLowerW(wfilter_lower);
    }
    for (int i = 0; i < sd->host_count; i++) {
        wchar_t *wname = utf8_to_wide(sd->hosts[i].name);
        if (!wname) continue;
        if (wfilter_lower[0]) {
            int nlen = (int)wcslen(wname);
            wchar_t *wname_lower = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
            if (wname_lower) {
                wcscpy(wname_lower, wname);
                CharLowerW(wname_lower);
                int match = (wcsstr(wname_lower, wfilter_lower) != NULL);
                free(wname_lower);
                if (!match) { free(wname); continue; }
            }
        }
        int idx = (int)SendMessageW(sd->hHostList, LB_ADDSTRING, 0, (LPARAM)wname);
        free(wname);
        if (idx != LB_ERR)
            SendMessageW(sd->hHostList, LB_SETITEMDATA, idx, i);
    }
}

static void fill_item_list(SelectData *sd, const wchar_t *wfilter)
{
    SendMessageW(sd->hItemList, LB_RESETCONTENT, 0, 0);
    wchar_t wfilter_lower[256] = {0};
    if (wfilter && *wfilter) {
        wcsncpy(wfilter_lower, wfilter, 255);
        CharLowerW(wfilter_lower);
    }
    for (int i = 0; i < sd->item_count; i++) {
        wchar_t *wname = utf8_to_wide(sd->items[i].name);
        if (!wname) continue;
        if (wfilter_lower[0]) {
            int nlen = (int)wcslen(wname);
            wchar_t *wname_lower = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
            if (wname_lower) {
                wcscpy(wname_lower, wname);
                CharLowerW(wname_lower);
                int match = (wcsstr(wname_lower, wfilter_lower) != NULL);
                free(wname_lower);
                if (!match) { free(wname); continue; }
            }
        }
        int idx = (int)SendMessageW(sd->hItemList, LB_ADDSTRING, 0, (LPARAM)wname);
        free(wname);
        if (idx != LB_ERR)
            SendMessageW(sd->hItemList, LB_SETITEMDATA, idx, i);
    }
}

/* ---------- Custom draw ---------- */

static void draw_header(HDC hdc, RECT *rc, SelectData *sd)
{
    RECT hr = *rc;
    hr.bottom = 82;

    /* Header bg */
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, sd->hHeaderBrush);
    Rectangle(hdc, hr.left - 1, hr.top - 1, hr.right + 1, hr.bottom);
    SelectObject(hdc, hOld);

    /* Separator */
    HPEN hSepPen = CreatePen(PS_SOLID, 1, CLR_SEP);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hSepPen);
    MoveToEx(hdc, hr.left, hr.bottom - 1, NULL);
    LineTo(hdc, hr.right, hr.bottom - 1);
    SelectObject(hdc, hOldPen);
    DeleteObject(hSepPen);

    /* Title */
    HFONT hOldFont = (HFONT)SelectObject(hdc, sd->hTitleFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_TEXT);

    RECT tr = hr;
    tr.top += 24;
    tr.left += MARGIN;
    tr.bottom -= 6;

    wchar_t *titleTxt = utf8_to_wide(i18n_str(S_SELECT_ITEM_TITLE));
    if (titleTxt) {
        DrawTextW(hdc, titleTxt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        free(titleTxt);
    }
    SelectObject(hdc, hOldFont);

    /* Accent underline */
    HPEN hAcc = CreatePen(PS_SOLID, 3, CLR_ACCENT);
    hOldPen = (HPEN)SelectObject(hdc, hAcc);
    MoveToEx(hdc, MARGIN, tr.bottom - 2, NULL);
    LineTo(hdc, MARGIN + 48, tr.bottom - 2);
    SelectObject(hdc, hOldPen);
    DeleteObject(hAcc);
}

/* ---------- WndProc ---------- */

static LRESULT CALLBACK SelectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SelectData *sd = (SelectData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            sd = (SelectData *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)sd);

            sd->hBgBrush     = CreateSolidBrush(CLR_BG);
            sd->hEditBrush   = CreateSolidBrush(CLR_EDIT_BG);
            sd->hHeaderBrush = CreateSolidBrush(CLR_HEADER_BG);
            sd->hListBrush   = CreateSolidBrush(CLR_LIST_BG);
            sd->hTitleFont   = MakeTitleFont();

            HFONT hFont = GetFont();
            int x1 = MARGIN;
            int x2 = MARGIN + HOST_W + COL_GAP;
            int y  = 95;

            /* --- Host column --- */
            i18n_create_label(hwnd, S_SEARCH, x1, y + 3, 45, 18, hFont);
            sd->hHostSearch = i18n_create_edit(hwnd, "", x1 + 45, y, HOST_W - 45, 22,
                                               IDC_HOST_SEARCH, hFont);
            y += 28;
            sd->hHostList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                x1, y, HOST_W, 210, hwnd, (HMENU)(INT_PTR)IDC_HOST_LIST, NULL, NULL);
            SendMessageW(sd->hHostList, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* --- Item column --- */
            y = 95;
            i18n_create_label(hwnd, S_SEARCH, x2, y + 3, 45, 18, hFont);
            sd->hSearch = i18n_create_edit(hwnd, "", x2 + 45, y, ITEM_W - 45, 22,
                                           IDC_ITEM_SEARCH, hFont);
            y += 28;
            sd->hItemList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                x2, y, ITEM_W, 210, hwnd, (HMENU)(INT_PTR)IDC_ITEM_LIST, NULL, NULL);
            SendMessageW(sd->hItemList, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* --- Widget type section --- */
            y += 225;  /* list height 210 + 15px gap */
            i18n_create_label(hwnd, S_WIDGET_TYPE, x1, y, 100, 18, hFont);
            y += 20;
            sd->hRadioGauge = i18n_create_button(hwnd, S_GAUGE,
                BS_AUTORADIOBUTTON | WS_GROUP, x1, y, 60, 20, IDC_RADIO_GAUGE, hFont);
            SendMessageW(sd->hRadioGauge, BM_SETCHECK, BST_CHECKED, 0);
            sd->hRadioCard = i18n_create_button(hwnd, S_CARD, BS_AUTORADIOBUTTON,
                x1 + 65, y, 55, 20, IDC_RADIO_CARD, hFont);
            sd->hRadioTrend = i18n_create_button(hwnd, S_TREND, BS_AUTORADIOBUTTON,
                x1 + 125, y, 65, 20, IDC_RADIO_TREND, hFont);

            /* --- Status --- */
            y += 24;
            sd->hStatus = i18n_create_label(hwnd, S_LOADING_HOSTS, x1, y, WIN_W - MARGIN * 2, 20, hFont);

            /* --- Buttons --- */
            int bY = WIN_H - 34 - 16;
            i18n_create_button(hwnd, S_CANCEL, 0,
                               WIN_W - MARGIN - 88 - 98, bY, 88, 34, IDC_CANCEL_BTN, hFont);
            i18n_create_button(hwnd, S_ADD_WIDGET, BS_DEFPUSHBUTTON,
                               WIN_W - MARGIN - 98, bY, 98, 34, IDC_ADD_BTN, hFont);
            EnableWindow(GetDlgItem(hwnd, IDC_ADD_BTN), FALSE);

            /* Size window */
            RECT rc = {0, 0, WIN_W, WIN_H};
            AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOZORDER);

            /* Start fetching hosts */
            sd->host_fetch.api = sd->api;
            sd->host_fetch.hwnd = hwnd;
            sd->host_fetch.done = 0;
            sd->host_thread = CreateThread(NULL, 0, fetch_hosts_thread, &sd->host_fetch, 0, NULL);
            return 0;
        }

        case WM_HOSTS_LOADED: {
            if (sd->host_fetch.error) {
                i18n_set_text_fmt(sd->hStatus, i18n_str(S_FAILED_LOAD_HOSTS_FMT),
                    zabbix_api_error());
            } else {
                sd->hosts = (ZabbixHost *)sd->host_fetch.data;
                sd->host_count = sd->host_fetch.count;
                fill_host_list(sd, NULL);
                i18n_set_text_fmt(sd->hStatus, i18n_str(S_HOSTS_LOADED_FMT), sd->host_count);
            }
            if (sd->host_thread) { CloseHandle(sd->host_thread); sd->host_thread = NULL; }
            return 0;
        }

        case WM_ITEMS_LOADED: {
            if (sd->items) { free(sd->items); sd->items = NULL; }
            if (sd->item_fetch.error) {
                i18n_set_text_fmt(sd->hStatus, i18n_str(S_FAILED_LOAD_ITEMS_FMT),
                    zabbix_api_error());
            } else {
                sd->items = (ZabbixItem *)sd->item_fetch.data;
                sd->item_count = sd->item_fetch.count;
                fill_item_list(sd, NULL);
                i18n_set_text_fmt(sd->hStatus, i18n_str(S_ITEMS_LOADED_FMT), sd->item_count);
            }
            sd->items_loading = 0;
            EnableWindow(sd->hHostList, TRUE);
            if (sd->item_thread) { CloseHandle(sd->item_thread); sd->item_thread = NULL; }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_HOST_LIST: {
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        if (sd->items_loading) {
                            if (sd->current_host_idx >= 0)
                                SendMessageA(sd->hHostList, LB_SETCURSEL,
                                    (WPARAM)sd->current_host_idx, 0);
                            break;
                        }
                        int idx = (int)SendMessageA(sd->hHostList, LB_GETCURSEL, 0, 0);
                        if (idx == LB_ERR) break;
                        int host_idx = (int)SendMessageA(sd->hHostList, LB_GETITEMDATA, idx, 0);
                        if (host_idx < 0 || host_idx >= sd->host_count) break;
                        sd->current_host_idx = host_idx;

                        SendMessageW(sd->hItemList, LB_RESETCONTENT, 0, 0);
                        set_text_utf8(sd->hStatus, i18n_str(S_LOADING_ITEMS));
                        EnableWindow(GetDlgItem(hwnd, IDC_ADD_BTN), FALSE);
                        EnableWindow(sd->hHostList, FALSE);
                        sd->items_loading = 1;

                        strncpy(sd->item_fetch.host_id_buf, sd->hosts[host_idx].id,
                            sizeof(sd->item_fetch.host_id_buf) - 1);
                        sd->item_fetch.host_id_buf[sizeof(sd->item_fetch.host_id_buf) - 1] = '\0';
                        sd->item_fetch.api = sd->api;
                        sd->item_fetch.hwnd = hwnd;
                        sd->item_fetch.done = 0;
                        sd->item_thread = CreateThread(NULL, 0, fetch_items_thread,
                            &sd->item_fetch, 0, NULL);
                    }
                    break;
                }

                case IDC_ITEM_LIST: {
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        int idx = (int)SendMessageA(sd->hItemList, LB_GETCURSEL, 0, 0);
                        if (idx == LB_ERR) {
                            sd->has_selection = 0;
                            EnableWindow(GetDlgItem(hwnd, IDC_ADD_BTN), FALSE);
                        } else {
                            int item_idx = (int)SendMessageA(sd->hItemList, LB_GETITEMDATA, idx, 0);
                            if (item_idx < 0 || item_idx >= sd->item_count) break;
                            sd->selected_item = sd->items[item_idx];
                            sd->has_selection = 1;
                            EnableWindow(GetDlgItem(hwnd, IDC_ADD_BTN), TRUE);
                            i18n_set_text_fmt(sd->hStatus, i18n_str(S_SELECTED_FMT),
                                sd->items[item_idx].name,
                                (sd->items[item_idx].units[0]) ? sd->items[item_idx].units
                                                               : i18n_str(S_NO_UNITS));
                        }
                    }
                    break;
                }

                case IDC_HOST_SEARCH: {
                    if (HIWORD(wParam) == EN_CHANGE) {
                        wchar_t wfilter[256];
                        GetWindowTextW(sd->hHostSearch, wfilter, 256);
                        fill_host_list(sd, wfilter);
                        if (sd->current_host_idx >= 0) {
                            for (int i = 0;
                                 i < (int)SendMessageA(sd->hHostList, LB_GETCOUNT, 0, 0);
                                 i++) {
                                int hidx = (int)SendMessageA(sd->hHostList, LB_GETITEMDATA, i, 0);
                                if (hidx == sd->current_host_idx) {
                                    SendMessageA(sd->hHostList, LB_SETCURSEL, i, 0);
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }

                case IDC_ITEM_SEARCH: {
                    if (HIWORD(wParam) == EN_CHANGE) {
                        wchar_t wfilter[256];
                        GetWindowTextW(sd->hSearch, wfilter, 256);
                        fill_item_list(sd, wfilter);
                    }
                    break;
                }

                case IDC_ADD_BTN: {
                    if (!sd->has_selection) break;

                    WidgetConfig wc;
                    memset(&wc, 0, sizeof(wc));

                    if (SendMessageA(sd->hRadioGauge, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        wc.type = WIDGET_GAUGE;
                    else if (SendMessageA(sd->hRadioCard, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        wc.type = WIDGET_CARD;
                    else
                        wc.type = WIDGET_TREND;

                    strncpy(wc.item_id, sd->selected_item.id, sizeof(wc.item_id) - 1);
                    strncpy(wc.item_name, sd->selected_item.name, sizeof(wc.item_name) - 1);
                    strncpy(wc.units, sd->selected_item.units, sizeof(wc.units) - 1);
                    wc.value_type = sd->selected_item.value_type;

                    if (sd->current_host_idx >= 0 && sd->current_host_idx < sd->host_count)
                        strncpy(wc.host_name, sd->hosts[sd->current_host_idx].name,
                            sizeof(wc.host_name) - 1);

                    wc.refresh_interval = 30;
                    wc.always_on_top = 1;
                    wc.bg_opacity = 200;
                    wc.accent_color = 0x4A90D9;
                    wc.trend_hours = 1;

                    switch (wc.type) {
                        case WIDGET_GAUGE:
                            wc.width = 200; wc.height = 220;
                            wc.gauge_min = 0; wc.gauge_max = 100;
                            wc.gauge_warn = 70; wc.gauge_crit = 90;
                            wc.gauge_warn_enabled = 1;
                            wc.gauge_crit_enabled = 1;
                            break;
                        case WIDGET_CARD:
                            wc.width = 240; wc.height = 100;
                            break;
                        case WIDGET_TREND:
                            wc.width = 320; wc.height = 180;
                            break;
                    }

                    /* Position: center on cursor's monitor with cascade */
                    {
                        POINT cursor;
                        GetCursorPos(&cursor);
                        HMONITOR hMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO mi;
                        mi.cbSize = sizeof(mi);
                        RECT wa;
                        if (GetMonitorInfoA(hMon, &mi)) wa = mi.rcWork;
                        else SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
                        int waW = wa.right - wa.left;
                        int waH = wa.bottom - wa.top;
                        wc.x = wa.left + (waW - wc.width) / 2;
                        wc.y = wa.top + (waH - wc.height) / 2;
                        DWORD tick = GetTickCount();
                        int offset = (int)((tick / 1000) % 8) * 25;
                        wc.x += offset;
                        wc.y += offset;
                        if (wc.x + wc.width > wa.right) wc.x = wa.right - wc.width;
                        if (wc.y + wc.height > wa.bottom) wc.y = wa.bottom - wc.height;
                        if (wc.x < wa.left) wc.x = wa.left;
                        if (wc.y < wa.top) wc.y = wa.top;
                    }

                    *sd->out_config = wc;
                    sd->result = 1;
                    DestroyWindow(hwnd);
                    break;
                }

                case IDC_CANCEL_BTN:
                    sd->result = 0;
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }

        /* ---- Dark-theme control coloring ---- */

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, CLR_TEXT);
            SetBkMode(hdcStatic, TRANSPARENT);
            SetBkColor(hdcStatic, CLR_BG);
            return (LRESULT)sd->hBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, CLR_TEXT);
            SetBkColor(hdcEdit, CLR_EDIT_BG);
            return (LRESULT)sd->hEditBrush;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdcList = (HDC)wParam;
            SetTextColor(hdcList, CLR_TEXT);
            SetBkColor(hdcList, CLR_LIST_BG);
            return (LRESULT)sd->hListBrush;
        }

        case WM_CTLCOLORBTN: {
            HDC hdcBtn = (HDC)wParam;
            SetTextColor(hdcBtn, CLR_TEXT);
            SetBkColor(hdcBtn, CLR_BG);
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
            draw_header(hdc, &rc, sd);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            sd->result = 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            if (sd->api) zabbix_api_abort(sd->api);
            if (sd->host_thread) {
                WaitForSingleObject(sd->host_thread, 10000);
                CloseHandle(sd->host_thread);
                sd->host_thread = NULL;
            }
            if (sd->item_thread) {
                WaitForSingleObject(sd->item_thread, 10000);
                CloseHandle(sd->item_thread);
                sd->item_thread = NULL;
            }
            if (sd->hosts) { free(sd->hosts); sd->hosts = NULL; }
            if (sd->items) { free(sd->items); sd->items = NULL; }
            if (sd->hBgBrush)     { DeleteObject(sd->hBgBrush);     sd->hBgBrush = NULL; }
            if (sd->hEditBrush)   { DeleteObject(sd->hEditBrush);   sd->hEditBrush = NULL; }
            if (sd->hHeaderBrush) { DeleteObject(sd->hHeaderBrush); sd->hHeaderBrush = NULL; }
            if (sd->hListBrush)   { DeleteObject(sd->hListBrush);   sd->hListBrush = NULL; }
            if (sd->hTitleFont)   { DeleteObject(sd->hTitleFont);   sd->hTitleFont = NULL; }
            break;
        }

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int ui_select_show(HINSTANCE hInstance, HWND parent, ZabbixAPI *api,
                   WidgetConfig *out_config)
{
    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SelectProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON));
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "ZabbixSelectDlg";
        RegisterClassExA(&wc);
        registered = 1;
    }

    SelectData sd;
    memset(&sd, 0, sizeof(sd));
    sd.api = api;
    sd.result = 0;
    sd.out_config = out_config;
    sd.current_host_idx = -1;

    HWND hwnd = CreateWindowExA(0, "ZabbixSelectDlg", "",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
        parent, NULL, hInstance, &sd);

    if (!hwnd) return 0;
    i18n_set_window_title(hwnd, S_SELECT_ITEM_TITLE);

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

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsWindow(hwnd)) break;
        if (!IsDialogMessageA(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return sd.result;
}
