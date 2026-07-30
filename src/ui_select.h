#ifndef UI_SELECT_H
#define UI_SELECT_H

#include <windows.h>
#include "config.h"
#include "zabbix_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Show item selection dialog.
 * Returns 1 if a widget was selected (out_config filled), 0 if cancelled. */
int ui_select_show(HINSTANCE hInstance, HWND parent, ZabbixAPI *api,
                   WidgetConfig *out_config);

#ifdef __cplusplus
}
#endif

#endif /* UI_SELECT_H */
