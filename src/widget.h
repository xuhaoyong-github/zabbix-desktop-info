#ifndef WIDGET_H
#define WIDGET_H

#include <windows.h>
#include "config.h"
#include "zabbix_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Custom message: sent to main window when a widget is removed */
#define WM_WIDGET_REMOVED (WM_USER + 201)

/* Register the widget window class. Call once at startup. */
int widget_register_class(HINSTANCE hInstance);

/* Create a widget window.
 * Returns HWND on success, NULL on failure.
 * hMainWnd: main window HWND for receiving WM_WIDGET_REMOVED notifications. */
HWND widget_create(HINSTANCE hInstance, ZabbixAPI *api,
                   AppConfig *config, int config_index,
                   int x, int y, HWND hMainWnd);

/* Update a widget's data (called by timer/worker thread) */
void widget_refresh(HWND hwnd);

/* Update widget config (after settings change) */
void widget_update_config(HWND hwnd, const WidgetConfig *new_cfg);

/* Get the config index for a widget */
int widget_get_config_index(HWND hwnd);

/* Set the config index for a widget (used after removing another widget) */
void widget_set_config_index(HWND hwnd, int index);

/* Set always-on-top */
void widget_set_topmost(HWND hwnd, int topmost);

#ifdef __cplusplus
}
#endif

#endif /* WIDGET_H */
