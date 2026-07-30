#ifndef UI_LOGIN_H
#define UI_LOGIN_H

#include <windows.h>
#include "config.h"
#include "zabbix_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Show login dialog. Returns 1 if login successful, 0 if cancelled.
 * On success, api->auth_token is set and config is updated. */
int ui_login_show(HINSTANCE hInstance, HWND parent, AppConfig *config, ZabbixAPI *api);

#ifdef __cplusplus
}
#endif

#endif /* UI_LOGIN_H */
