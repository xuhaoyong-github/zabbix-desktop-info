#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "ui_select.h"
#include "zabbix_api.h"
#include "config.h"

#pragma comment(lib, "comctl32.lib")

/* Convert UTF-8 string to UTF-16 (wide). Returns malloc'd buffer, caller frees.
 * Returns NULL if input is NULL. */
static wchar_t *utf8_to_wide(const char *utf8)
{
    if (!utf8) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(len * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, len);
    return w;
}

/* Convert UTF-16 to ANSI (system code page). Returns malloc'd buffer. */
static char *wide_to_acp(const wchar_t *w)
{
    if (!w) return NULL;
    int len = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char *a = (char *)malloc(len);
    if (!a) return NULL;
    WideCharToMultiByte(CP_ACP, 0, w, -1, a, len, NULL, NULL);
    return a;
}

/* Convert UTF-8 to ANSI via UTF-16, for ASCII-only APIs */
static char *utf8_to_acp(const char *utf8)
{
    wchar_t *w = utf8_to_wide(utf8);
    if (!w) return NULL;
    char *a = wide_to_acp(w);
    free(w);
    return a;
}

/* Set control text from a UTF-8 string (handles Unicode correctly) */
static void set_text_utf8(HWND hCtrl, const char *utf8)
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

typedef struct {
    ZabbixAPI *api;
    HWND hwnd;
    int done;
    int error;
    /* Output */
    void *data;     /* hosts or items */
    int count;
    char host_id_buf[64]; /* safe copy of host id for item fetch thread */
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
    int items_loading;  /* flag: item fetch thread is running */

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
} SelectData;

static HFONT GetFont(void)
{
    return CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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

    /* Lowercase the filter for case-insensitive search */
    wchar_t wfilter_lower[256] = {0};
    if (wfilter && *wfilter) {
        wcsncpy(wfilter_lower, wfilter, 255);
        CharLowerW(wfilter_lower);
    }

    for (int i = 0; i < sd->host_count; i++) {
        wchar_t *wname = utf8_to_wide(sd->hosts[i].name);
        if (!wname) continue;

        /* Apply filter: case-insensitive Unicode substring search */
        if (wfilter_lower[0]) {
            /* Use a separate lowercase copy for comparison so the
             * displayed name keeps its original case */
            int nlen = (int)wcslen(wname);
            wchar_t *wname_lower = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
            if (wname_lower) {
                wcscpy(wname_lower, wname);
                CharLowerW(wname_lower);
                int match = (wcsstr(wname_lower, wfilter_lower) != NULL);
                free(wname_lower);
                if (!match) {
                    free(wname);
                    continue;
                }
            }
        }

        int idx = LB_ERR;
        if (wname) {
            idx = (int)SendMessageW(sd->hHostList, LB_ADDSTRING, 0, (LPARAM)wname);
        }
        if (idx == LB_ERR) {
            /* fallback: try ANSI with raw string */
            char *aname = utf8_to_acp(sd->hosts[i].name);
            if (aname) {
                idx = (int)SendMessageA(sd->hHostList, LB_ADDSTRING, 0, (LPARAM)aname);
                free(aname);
            }
        }
        free(wname);
        if (idx != LB_ERR)
            SendMessageW(sd->hHostList, LB_SETITEMDATA, idx, i);
    }
}

static void fill_item_list(SelectData *sd, const wchar_t *wfilter)
{
    SendMessageW(sd->hItemList, LB_RESETCONTENT, 0, 0);

    /* Lowercase the filter for case-insensitive search */
    wchar_t wfilter_lower[256] = {0};
    if (wfilter && *wfilter) {
        wcsncpy(wfilter_lower, wfilter, 255);
        CharLowerW(wfilter_lower);
    }

    for (int i = 0; i < sd->item_count; i++) {
        wchar_t *wname = utf8_to_wide(sd->items[i].name);
        if (!wname) continue;

        /* Apply filter: case-insensitive Unicode substring search */
        if (wfilter_lower[0]) {
            /* Use a separate lowercase copy for comparison so the
             * displayed name keeps its original case */
            int nlen = (int)wcslen(wname);
            wchar_t *wname_lower = (wchar_t *)malloc((nlen + 1) * sizeof(wchar_t));
            if (wname_lower) {
                wcscpy(wname_lower, wname);
                CharLowerW(wname_lower);
                int match = (wcsstr(wname_lower, wfilter_lower) != NULL);
                free(wname_lower);
                if (!match) {
                    free(wname);
                    continue;
                }
            }
        }

        int idx = (int)SendMessageW(sd->hItemList, LB_ADDSTRING, 0, (LPARAM)wname);
        free(wname);
        if (idx != LB_ERR)
            SendMessageW(sd->hItemList, LB_SETITEMDATA, idx, i);
    }
}

static LRESULT CALLBACK SelectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SelectData *sd = (SelectData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            sd = (SelectData *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)sd);

            HFONT hFont = GetFont();
            HWND hLbl;

            /* Labels */
            hLbl = CreateWindowExA(0, "STATIC", "Hosts:", WS_CHILD | WS_VISIBLE,
                10, 10, 150, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            hLbl = CreateWindowExA(0, "STATIC", "Monitor Items:", WS_CHILD | WS_VISIBLE,
                200, 10, 200, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Host list */
            sd->hHostList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_SORT,
                10, 30, 180, 210, hwnd, (HMENU)(INT_PTR)IDC_HOST_LIST, NULL, NULL);
            SendMessageA(sd->hHostList, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Item list */
            sd->hItemList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
                200, 50, 320, 200, hwnd, (HMENU)(INT_PTR)IDC_ITEM_LIST, NULL, NULL);
            SendMessageA(sd->hItemList, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Host search box */
            hLbl = CreateWindowExA(0, "STATIC", "Search:", WS_CHILD | WS_VISIBLE,
                10, 253, 50, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            sd->hHostSearch = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                55, 250, 135, 22, hwnd, (HMENU)(INT_PTR)IDC_HOST_SEARCH, NULL, NULL);
            SendMessageA(sd->hHostSearch, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Item search box */
            hLbl = CreateWindowExA(0, "STATIC", "Search:", WS_CHILD | WS_VISIBLE,
                200, 253, 50, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            sd->hSearch = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                255, 250, 265, 22, hwnd, (HMENU)(INT_PTR)IDC_ITEM_SEARCH, NULL, NULL);
            SendMessageA(sd->hSearch, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Widget type radio buttons */
            hLbl = CreateWindowExA(0, "STATIC", "Widget Type:", WS_CHILD | WS_VISIBLE,
                10, 282, 90, 18, hwnd, NULL, NULL, NULL);
            SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            sd->hRadioGauge = CreateWindowExA(0, "BUTTON", "Gauge",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                10, 302, 60, 20, hwnd, (HMENU)(INT_PTR)IDC_RADIO_GAUGE, NULL, NULL);
            SendMessageA(sd->hRadioGauge, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageA(sd->hRadioGauge, BM_SETCHECK, BST_CHECKED, 0);

            sd->hRadioCard = CreateWindowExA(0, "BUTTON", "Card",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                75, 302, 55, 20, hwnd, (HMENU)(INT_PTR)IDC_RADIO_CARD, NULL, NULL);
            SendMessageA(sd->hRadioCard, WM_SETFONT, (WPARAM)hFont, TRUE);

            sd->hRadioTrend = CreateWindowExA(0, "BUTTON", "Trend",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                135, 302, 60, 20, hwnd, (HMENU)(INT_PTR)IDC_RADIO_TREND, NULL, NULL);
            SendMessageA(sd->hRadioTrend, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Status */
            sd->hStatus = CreateWindowExA(0, "STATIC", "Loading hosts...",
                WS_CHILD | WS_VISIBLE, 10, 325, 400, 18, hwnd,
                (HMENU)(INT_PTR)IDC_STATUS_LABEL, NULL, NULL);
            SendMessageA(sd->hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Buttons */
            CreateWindowExA(0, "BUTTON", "Add Widget", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                340, 350, 90, 30, hwnd, (HMENU)(INT_PTR)IDC_ADD_BTN, NULL, NULL);
            SendMessageA(GetDlgItem(hwnd, IDC_ADD_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_ADD_BTN), FALSE);

            CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
                435, 350, 80, 30, hwnd, (HMENU)(INT_PTR)IDC_CANCEL_BTN, NULL, NULL);
            SendMessageA(GetDlgItem(hwnd, IDC_CANCEL_BTN), WM_SETFONT, (WPARAM)hFont, TRUE);

            /* Size window */
            RECT rc = {0, 0, 530, 390};
            AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
            SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

            /* Start fetching hosts */
            sd->host_fetch.api = sd->api;
            sd->host_fetch.hwnd = hwnd;
            sd->host_fetch.done = 0;
            sd->host_thread = CreateThread(NULL, 0, fetch_hosts_thread, &sd->host_fetch, 0, NULL);
            return 0;
        }

        case WM_HOSTS_LOADED: {
            if (sd->host_fetch.error) {
                char buf[600];
                snprintf(buf, sizeof(buf), "Failed to load hosts: %s", zabbix_api_error());
                set_text_utf8(sd->hStatus, buf);
            } else {
                sd->hosts = (ZabbixHost *)sd->host_fetch.data;
                sd->host_count = sd->host_fetch.count;
                fill_host_list(sd, NULL);
                char buf[64];
                snprintf(buf, sizeof(buf), "%d hosts loaded. Select a host.", sd->host_count);
                set_text_utf8(sd->hStatus, buf);
            }
            if (sd->host_thread) { CloseHandle(sd->host_thread); sd->host_thread = NULL; }
            return 0;
        }

        case WM_ITEMS_LOADED: {
            /* Free previous items */
            if (sd->items) { free(sd->items); sd->items = NULL; }

            if (sd->item_fetch.error) {
                char buf[600];
                snprintf(buf, sizeof(buf), "Failed to load items: %s", zabbix_api_error());
                set_text_utf8(sd->hStatus, buf);
            } else {
                sd->items = (ZabbixItem *)sd->item_fetch.data;
                sd->item_count = sd->item_fetch.count;
                fill_item_list(sd, NULL);
                char buf[64];
                snprintf(buf, sizeof(buf), "%d items loaded.", sd->item_count);
                set_text_utf8(sd->hStatus, buf);
            }
            sd->items_loading = 0;
            EnableWindow(sd->hHostList, TRUE);  /* re-enable host list */
            if (sd->item_thread) { CloseHandle(sd->item_thread); sd->item_thread = NULL; }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_HOST_LIST: {
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        /* Ignore if items are still loading */
                        if (sd->items_loading) {
                            /* Revert selection to previously selected host */
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

                        /* Clear item list and start loading */
                        SendMessageW(sd->hItemList, LB_RESETCONTENT, 0, 0);
                        set_text_utf8(sd->hStatus, "Loading items...");
                        EnableWindow(GetDlgItem(hwnd, IDC_ADD_BTN), FALSE);
                        EnableWindow(sd->hHostList, FALSE);
                        sd->items_loading = 1;

                        /* Safe copy of host id for the thread */
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

                            /* Show item details in Unicode */
                            wchar_t wbuf[512];
                            wchar_t *wname = utf8_to_wide(sd->items[item_idx].name);
                            wchar_t *wunits = utf8_to_wide(sd->items[item_idx].units);
                            if (wname) {
                                _snwprintf(wbuf, 512, L"Selected: %s (%s)",
                                          wname,
                                          (wunits && wunits[0]) ? wunits : L"no units");
                                SetWindowTextW(sd->hStatus, wbuf);
                            } else {
                                SetWindowTextA(sd->hStatus, "Selected");
                            }
                            if (wname) free(wname);
                            if (wunits) free(wunits);
                        }
                    }
                    break;
                }

                case IDC_HOST_SEARCH: {
                    if (HIWORD(wParam) == EN_CHANGE) {
                        wchar_t wfilter[256];
                        GetWindowTextW(sd->hHostSearch, wfilter, 256);
                        fill_host_list(sd, wfilter);
                        /* Restore selection if a host was selected */
                        if (sd->current_host_idx >= 0) {
                            /* Find the host in the filtered list and select it */
                            for (int i = 0; i < (int)SendMessageA(sd->hHostList, LB_GETCOUNT, 0, 0); i++) {
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

                    /* Determine widget type */
                    if (SendMessageA(sd->hRadioGauge, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        wc.type = WIDGET_GAUGE;
                    else if (SendMessageA(sd->hRadioCard, BM_GETCHECK, 0, 0) == BST_CHECKED)
                        wc.type = WIDGET_CARD;
                    else
                        wc.type = WIDGET_TREND;

                    /* Copy item info */
                    strncpy(wc.item_id, sd->selected_item.id, sizeof(wc.item_id) - 1);
                    strncpy(wc.item_name, sd->selected_item.name, sizeof(wc.item_name) - 1);
                    strncpy(wc.units, sd->selected_item.units, sizeof(wc.units) - 1);
                    wc.value_type = sd->selected_item.value_type;

                    /* Host name */
                    if (sd->current_host_idx >= 0 && sd->current_host_idx < sd->host_count)
                        strncpy(wc.host_name, sd->hosts[sd->current_host_idx].name, sizeof(wc.host_name) - 1);

                    /* Defaults */
                    wc.refresh_interval = 30;
                    wc.always_on_top = 1;
                    wc.bg_opacity = 200;
                    wc.accent_color = 0x4A90D9;
                    wc.trend_hours = 1;

                    /* Type-specific defaults */
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
                        if (GetMonitorInfoA(hMon, &mi)) {
                            wa = mi.rcWork;
                        } else {
                            SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
                        }
                        int waW = wa.right - wa.left;
                        int waH = wa.bottom - wa.top;
                        wc.x = wa.left + (waW - wc.width) / 2;
                        wc.y = wa.top + (waH - wc.height) / 2;
                        /* Cascade offset so multiple new widgets don't overlap */
                        DWORD tick = GetTickCount();
                        int offset = (int)((tick / 1000) % 8) * 25;
                        wc.x += offset;
                        wc.y += offset;
                        /* Clamp to stay within work area */
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

        case WM_CLOSE:
            sd->result = 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            /* Wait for threads */
            if (sd->host_thread) {
                WaitForSingleObject(sd->host_thread, 3000);
                CloseHandle(sd->host_thread);
            }
            if (sd->item_thread) {
                WaitForSingleObject(sd->item_thread, 3000);
                CloseHandle(sd->item_thread);
            }
            /* Free data */
            if (sd->hosts) free(sd->hosts);
            if (sd->items) free(sd->items);
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
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
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

    HWND hwnd = CreateWindowExA(0, "ZabbixSelectDlg", "Select Monitor Item",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 530, 390,
        parent, NULL, hInstance, &sd);

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

    return sd.result;
}
