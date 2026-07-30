#ifndef RENDER_H
#define RENDER_H

#include <windows.h>
#include "config.h"
#include "zabbix_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize GDI+ */
int render_init(void);

/* Shutdown GDI+ */
void render_shutdown(void);

/* Render a widget to a 32-bit premultiplied ARGB DIB section.
 *
 * Parameters:
 *   hdc         - output: HDC compatible with the DIB
 *   hBitmap     - output: HBITMAP of the DIB section
 *   width       - widget width in pixels
 *   height      - widget height in pixels
 *   type        - widget type (WIDGET_GAUGE, WIDGET_CARD, WIDGET_TREND)
 *   value       - current numeric value
 *   value_str   - current value as string (for display)
 *   title       - widget title (usually item name)
 *   units       - value units
 *   cfg         - widget configuration
 *   points      - history data points (for trend chart)
 *   point_count - number of history points
 *
 * Returns 0 on success.
 */
int render_widget(HDC *hdc, HBITMAP *hBitmap,
                  int width, int height,
                  WidgetType type,
                  double value, const char *value_str,
                  const char *title, const char *units,
                  const WidgetConfig *cfg,
                  const ZabbixHistoryPoint *points, int point_count);

/* Free resources from a previous render_widget call */
void render_free(HDC hdc, HBITMAP hBitmap);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_H */
