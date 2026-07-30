#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "render.h"

using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")

static ULONG_PTR g_gdiplusToken = 0;

int render_init(void)
{
    GdiplusStartupInput input;
    GdiplusStartupOutput output;
    Status s = GdiplusStartup(&g_gdiplusToken, &input, &output);
    return (s == Ok) ? 0 : -1;
}

void render_shutdown(void)
{
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

void render_free(HDC hdc, HBITMAP hBitmap)
{
    if (hBitmap) DeleteObject(hBitmap);
    if (hdc) DeleteDC(hdc);
}

/* Create a 32-bit premultiplied ARGB DIB section */
static HBITMAP create_argb_dib(HDC *outHdc, int width, int height, void **outBits)
{
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(NULL, screenDC);

    void *bits = NULL;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hBmp) {
        DeleteDC(memDC);
        return NULL;
    }
    SelectObject(memDC, hBmp);
    memset(bits, 0, width * height * 4);

    if (outHdc) *outHdc = memDC;
    if (outBits) *outBits = bits;
    return hBmp;
}

/* Premultiply alpha for UpdateLayeredWindow compatibility */
static void premultiply_alpha(unsigned char *bits, int width, int height)
{
    int total = width * height;
    for (int i = 0; i < total; i++) {
        unsigned char a = bits[3];
        if (a > 0 && a < 255) {
            bits[0] = (bits[0] * a) / 255;
            bits[1] = (bits[1] * a) / 255;
            bits[2] = (bits[2] * a) / 255;
        }
        bits += 4;
    }
}

/* UTF-8 to wide string helper */
static WCHAR *utf8_to_wide(const char *s)
{
    if (!s || !*s) {
        WCHAR *w = new WCHAR[1];
        w[0] = 0;
        return w;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = new WCHAR[len];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, len);
    return w;
}

/* Helper: draw rounded rectangle path */
static void draw_rounded_rect(Graphics *g, Pen *pen, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    g->DrawPath(pen, &path);
}

static void fill_rounded_rect(Graphics *g, Brush *brush, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180, 90);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    g->FillPath(brush, &path);
}

/* Convert 0xBBGGRR int to Gdiplus::Color */
static Color int_to_color(int rgb, int alpha = 255)
{
    return Color(alpha, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb));
}

/* ============ GAUGE RENDERING ============ */

static void render_gauge(Graphics *g, int width, int height,
                         double value, const char *value_str,
                         const char *title, const char *units,
                         const WidgetConfig *cfg)
{
    /* Background panel */
    int bgAlpha = cfg->bg_opacity > 0 ? cfg->bg_opacity : 200;
    SolidBrush bgBrush(Color(bgAlpha, 28, 30, 40));
    fill_rounded_rect(g, &bgBrush, 0, 0, (REAL)width, (REAL)height, 12);

    /* Subtle border */
    Pen borderPen(Color(80, 80, 90, 110), 1.0f);
    draw_rounded_rect(g, &borderPen, 0.5f, 0.5f, (REAL)width - 1, (REAL)height - 1, 12);

    /* Gauge geometry: 270 degree arc */
    REAL cx = width / 2.0f;
    REAL cy = height / 2.0f + 5;
    REAL radius = (width < height ? width : height) / 2.0f - 25;
    if (radius < 30) radius = 30;

    REAL startAngle = 135.0f;
    REAL sweepAngle = 270.0f;

    double minVal = cfg->gauge_min;
    double maxVal = cfg->gauge_max;
    if (maxVal <= minVal) { minVal = 0; maxVal = 100; }

    /* Clamp value */
    double clampedVal = value;
    if (clampedVal < minVal) clampedVal = minVal;
    if (clampedVal > maxVal) clampedVal = maxVal;
    double ratio = (clampedVal - minVal) / (maxVal - minVal);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    /* Background arc (track) */
    Pen trackPen(Color(60, 70, 80, 95), radius * 0.12f);
    trackPen.SetStartCap(LineCapRound);
    trackPen.SetEndCap(LineCapRound);
    g->DrawArc(&trackPen, cx - radius, cy - radius, radius * 2, radius * 2,
               startAngle, sweepAngle);

    /* Determine arc color based on thresholds */
    Color arcColor = int_to_color(cfg->accent_color, 255);
    if (cfg->gauge_crit_enabled && value >= cfg->gauge_crit) {
        arcColor = Color(255, 220, 60, 60);   /* red */
    } else if (cfg->gauge_warn_enabled && value >= cfg->gauge_warn) {
        arcColor = Color(255, 240, 180, 40);  /* yellow/orange */
    } else {
        arcColor = Color(255, 80, 200, 120);  /* green */
    }

    /* Value arc */
    Pen valuePen(arcColor, radius * 0.12f);
    valuePen.SetStartCap(LineCapRound);
    valuePen.SetEndCap(LineCapRound);
    g->DrawArc(&valuePen, cx - radius, cy - radius, radius * 2, radius * 2,
               startAngle, (REAL)(sweepAngle * ratio));

    /* Needle */
    REAL needleAngle = startAngle + (REAL)(sweepAngle * ratio);
    /* Convert to radians (GDI+ angles are in degrees, clockwise from 3 o'clock) */
    double rad = needleAngle * 3.14159265358979 / 180.0;
    REAL needleLen = radius * 0.75f;
    REAL ex = cx + (REAL)(cos(rad) * needleLen);
    REAL ey = cy + (REAL)(sin(rad) * needleLen);

    Pen needlePen(Color(240, 230, 230, 240), 3.0f);
    needlePen.SetEndCap(LineCapRound);
    g->DrawLine(&needlePen, cx, cy, ex, ey);

    /* Center dot */
    SolidBrush dotBrush(Color(240, 230, 230, 240));
    g->FillEllipse(&dotBrush, cx - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    SolidBrush dotInner(Color(255, 50, 55, 70));
    g->FillEllipse(&dotInner, cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);

    /* Value text */
    FontFamily valFamily(L"Segoe UI");
    Font valFont(&valFamily, radius * 0.35f, FontStyleBold, UnitPixel);
    SolidBrush valBrush(Color(245, 240, 240, 250));

    StringFormat valFormat;
    valFormat.SetAlignment(StringAlignmentCenter);
    valFormat.SetLineAlignment(StringAlignmentCenter);

    WCHAR *wval = utf8_to_wide(value_str);
    /* Truncate long values */
    WCHAR valDisplay[64];
    if (wcslen(wval) > 10) {
        wcsncpy(valDisplay, wval, 10);
        valDisplay[10] = 0;
    } else {
        wcscpy(valDisplay, wval);
    }
    g->DrawString(valDisplay, -1, &valFont,
                  PointF(cx, cy - radius * 0.25f), &valFormat, &valBrush);
    delete[] wval;

    /* Units text */
    if (units && *units) {
        Font unitFont(&valFamily, radius * 0.15f, FontStyleRegular, UnitPixel);
        SolidBrush unitBrush(Color(180, 160, 165, 180));
        WCHAR *wunits = utf8_to_wide(units);
        g->DrawString(wunits, -1, &unitFont,
                      PointF(cx, cy + radius * 0.2f), &valFormat, &unitBrush);
        delete[] wunits;
    }

    /* Min/Max labels */
    Font minMaxFont(&valFamily, 10, FontStyleRegular, UnitPixel);
    SolidBrush minMaxBrush(Color(140, 130, 135, 150));
    StringFormat leftFormat, rightFormat;
    leftFormat.SetAlignment(StringAlignmentNear);
    rightFormat.SetAlignment(StringAlignmentFar);

    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", minVal);
        WCHAR *w = utf8_to_wide(buf);
        g->DrawString(w, -1, &minMaxFont,
                      PointF(cx - radius, cy + radius * 0.55f), &leftFormat, &minMaxBrush);
        delete[] w;
        snprintf(buf, sizeof(buf), "%.0f", maxVal);
        w = utf8_to_wide(buf);
        g->DrawString(w, -1, &minMaxFont,
                      PointF(cx + radius, cy + radius * 0.55f), &rightFormat, &minMaxBrush);
        delete[] w;
    }

    /* Title at bottom */
    if (title && *title) {
        Font titleFont(&valFamily, 11, FontStyleRegular, UnitPixel);
        SolidBrush titleBrush(Color(190, 170, 175, 190));
        StringFormat titleFormat;
        titleFormat.SetAlignment(StringAlignmentCenter);
        titleFormat.SetTrimming(StringTrimmingEllipsisPath);
        RectF titleRect(5, height - 22, (REAL)(width - 10), 18);
        WCHAR *wtitle = utf8_to_wide(title);
        g->DrawString(wtitle, -1, &titleFont,
                      PointF((REAL)width / 2, (REAL)height - 22), &titleFormat, &titleBrush);
        delete[] wtitle;
    }
}

/* ============ CARD RENDERING ============ */

static void render_card(Graphics *g, int width, int height,
                        double value, const char *value_str,
                        const char *title, const char *units,
                        const WidgetConfig *cfg)
{
    /* Background panel */
    int bgAlpha = cfg->bg_opacity > 0 ? cfg->bg_opacity : 200;
    SolidBrush bgBrush(Color(bgAlpha, 28, 30, 40));
    fill_rounded_rect(g, &bgBrush, 0, 0, (REAL)width, (REAL)height, 10);

    /* Accent bar on left */
    Color accent = int_to_color(cfg->accent_color, 255);
    SolidBrush accentBrush(accent);
    /* Simple: just fill a thin rectangle */
    RectF accentRect(0, 0, 4.0f, (REAL)height);
    g->FillRectangle(&accentBrush, accentRect);

    /* Border */
    Pen borderPen(Color(60, 80, 90, 110), 1.0f);
    draw_rounded_rect(g, &borderPen, 0.5f, 0.5f, (REAL)width - 1, (REAL)height - 1, 10);

    FontFamily family(L"Segoe UI");

    /* Title (top) */
    if (title && *title) {
        Font titleFont(&family, 13, FontStyleRegular, UnitPixel);
        SolidBrush titleBrush(Color(170, 160, 165, 180));
        StringFormat tf;
        tf.SetAlignment(StringAlignmentNear);
        tf.SetTrimming(StringTrimmingEllipsisPath);
        RectF titleRect(14, 8, (REAL)(width - 24), 20);
        WCHAR *w = utf8_to_wide(title);
        g->DrawString(w, -1, &titleFont, titleRect, &tf, &titleBrush);
        delete[] w;
    }

    /* Value (large, centered) */
    Font valFont2(&family, height > 100 ? 32.0f : 24.0f, FontStyleBold, UnitPixel);
    SolidBrush valBrush(Color(245, 240, 240, 250));
    StringFormat vf;
    vf.SetAlignment(StringAlignmentNear);
    vf.SetLineAlignment(StringAlignmentCenter);

    /* Build value + units string */
    char valbuf[128];
    if (units && *units)
        snprintf(valbuf, sizeof(valbuf), "%s %s", value_str ? value_str : "N/A", units);
    else
        snprintf(valbuf, sizeof(valbuf), "%s", value_str ? value_str : "N/A");

    WCHAR *wval = utf8_to_wide(valbuf);
    RectF valRect(14, (REAL)(height * 0.35), (REAL)(width - 24), (REAL)(height * 0.5));
    g->DrawString(wval, -1, &valFont2, valRect, &vf, &valBrush);
    delete[] wval;
}

/* ============ TREND CHART RENDERING ============ */

static void render_trend(Graphics *g, int width, int height,
                         double value, const char *value_str,
                         const char *title, const char *units,
                         const WidgetConfig *cfg,
                         const ZabbixHistoryPoint *points, int point_count)
{
    /* Background panel */
    int bgAlpha = cfg->bg_opacity > 0 ? cfg->bg_opacity : 200;
    SolidBrush bgBrush(Color(bgAlpha, 28, 30, 40));
    fill_rounded_rect(g, &bgBrush, 0, 0, (REAL)width, (REAL)height, 10);

    Pen borderPen(Color(60, 80, 90, 110), 1.0f);
    draw_rounded_rect(g, &borderPen, 0.5f, 0.5f, (REAL)width - 1, (REAL)height - 1, 10);

    FontFamily family(L"Segoe UI");

    /* Title */
    if (title && *title) {
        Font titleFont(&family, 12, FontStyleRegular, UnitPixel);
        SolidBrush titleBrush(Color(180, 170, 175, 190));
        StringFormat tf;
        tf.SetAlignment(StringAlignmentNear);
        tf.SetTrimming(StringTrimmingEllipsisPath);
        RectF titleRect(12, 6, (REAL)(width - 24), 18);
        WCHAR *w = utf8_to_wide(title);
        g->DrawString(w, -1, &titleFont, titleRect, &tf, &titleBrush);
        delete[] w;
    }

    /* Current value (top right) */
    {
        Font cvFont(&family, 12, FontStyleBold, UnitPixel);
        SolidBrush cvBrush(Color(245, 240, 240, 250));
        StringFormat cf;
        cf.SetAlignment(StringAlignmentFar);
        char cvbuf[128];
        if (units && *units)
            snprintf(cvbuf, sizeof(cvbuf), "%s %s", value_str ? value_str : "N/A", units);
        else
            snprintf(cvbuf, sizeof(cvbuf), "%s", value_str ? value_str : "N/A");
        WCHAR *w = utf8_to_wide(cvbuf);
        RectF cvRect(12, 6, (REAL)(width - 24), 18);
        g->DrawString(w, -1, &cvFont, cvRect, &cf, &cvBrush);
        delete[] w;
    }

    /* Chart area */
    REAL chartLeft = 35;
    REAL chartRight = width - 8;
    REAL chartTop = 32;
    REAL chartBottom = height - 20;
    REAL chartW = chartRight - chartLeft;
    REAL chartH = chartBottom - chartTop;

    if (chartW < 10 || chartH < 10 || point_count < 1) {
        /* No data */
        Font nf(&family, 11, FontStyleRegular, UnitPixel);
        SolidBrush nb(Color(120, 120, 125, 140));
        StringFormat nfmt;
        nfmt.SetAlignment(StringAlignmentCenter);
        nfmt.SetLineAlignment(StringAlignmentCenter);
        g->DrawString(L"No data", -1, &nf,
                      PointF((REAL)width / 2, (REAL)(chartTop + chartH / 2)), &nfmt, &nb);
        return;
    }

    /* Find min/max */
    double minV = points[0].value, maxV = points[0].value;
    for (int i = 1; i < point_count; i++) {
        if (points[i].value < minV) minV = points[i].value;
        if (points[i].value > maxV) maxV = points[i].value;
    }
    if (maxV - minV < 0.0001) { maxV += 1; minV -= 1; }
    double range = maxV - minV;
    /* Add 10% padding */
    minV -= range * 0.1;
    maxV += range * 0.1;
    range = maxV - minV;

    /* Grid lines */
    Pen gridPen(Color(30, 70, 80, 95), 1.0f);
    gridPen.SetDashStyle(DashStyleDot);
    for (int i = 0; i <= 4; i++) {
        REAL y = chartTop + chartH * i / 4;
        g->DrawLine(&gridPen, chartLeft, y, chartRight, y);
    }

    /* Y axis labels */
    Font axisFont(&family, 9, FontStyleRegular, UnitPixel);
    SolidBrush axisBrush(Color(130, 130, 135, 150));
    StringFormat leftFmt;
    leftFmt.SetAlignment(StringAlignmentFar);
    leftFmt.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i <= 4; i++) {
        REAL y = chartTop + chartH * i / 4;
        double v = maxV - (range * i / 4);
        char buf[32];
        if (fabs(v) >= 1000) snprintf(buf, sizeof(buf), "%.0fK", v / 1000);
        else if (fabs(v) >= 100) snprintf(buf, sizeof(buf), "%.0f", v);
        else if (fabs(v) >= 10) snprintf(buf, sizeof(buf), "%.1f", v);
        else snprintf(buf, sizeof(buf), "%.2f", v);
        WCHAR *w = utf8_to_wide(buf);
        RectF lblRect(2, y - 7, chartLeft - 4, 14);
        g->DrawString(w, -1, &axisFont, lblRect, &leftFmt, &axisBrush);
        delete[] w;
    }

    /* Calculate points */
    long long tMin = points[0].clock;
    long long tMax = points[point_count - 1].clock;
    if (tMax - tMin < 1) tMax = tMin + 1;

    Point *pts = new Point[point_count];
    for (int i = 0; i < point_count; i++) {
        REAL px = chartLeft + (REAL)(points[i].clock - tMin) / (tMax - tMin) * chartW;
        REAL py = chartTop + chartH * (1.0f - (REAL)((points[i].value - minV) / range));
        if (py < chartTop) py = chartTop;
        if (py > chartBottom) py = chartBottom;
        pts[i].X = (INT)px;
        pts[i].Y = (INT)py;
    }

    /* Fill area under the line */
    Color lineColor = int_to_color(cfg->accent_color, 255);
    if (cfg->accent_color == 0)
        lineColor = Color(255, 90, 160, 220);

    Point *fillPts = new Point[point_count + 2];
    for (int i = 0; i < point_count; i++)
        fillPts[i] = pts[i];
    fillPts[point_count] = Point((INT)chartRight, (INT)chartBottom);
    fillPts[point_count + 1] = Point((INT)chartLeft, (INT)chartBottom);

    /* Gradient fill */
    LinearGradientBrush fillBrush(
        RectF(chartLeft, chartTop, chartW, chartH),
        Color(60, lineColor.GetR(), lineColor.GetG(), lineColor.GetB()),
        Color(5, lineColor.GetR(), lineColor.GetG(), lineColor.GetB()),
        LinearGradientModeVertical
    );
    g->FillPolygon(&fillBrush, fillPts, point_count + 2);
    delete[] fillPts;

    /* Draw line */
    Pen linePen(lineColor, 2.0f);
    linePen.SetLineJoin(LineJoinRound);
    for (int i = 0; i < point_count - 1; i++) {
        g->DrawLine(&linePen, pts[i], pts[i + 1]);
    }

    /* Draw last point marker */
    SolidBrush ptBrush(lineColor);
    g->FillEllipse(&ptBrush, pts[point_count - 1].X - 3, pts[point_count - 1].Y - 3, 6, 6);

    /* X axis time labels */
    StringFormat btmFmt;
    btmFmt.SetAlignment(StringAlignmentNear);
    {
        WCHAR *w = utf8_to_wide("now");
        g->DrawString(w, -1, &axisFont,
                      PointF(chartRight - 20, chartBottom + 4), &btmFmt, &axisBrush);
        delete[] w;
    }

    delete[] pts;
}

/* ============ MAIN RENDER FUNCTION ============ */

int render_widget(HDC *outHdc, HBITMAP *outBitmap,
                  int width, int height,
                  WidgetType type,
                  double value, const char *value_str,
                  const char *title, const char *units,
                  const WidgetConfig *cfg,
                  const ZabbixHistoryPoint *points, int point_count)
{
    if (!outHdc || !outBitmap || width <= 0 || height <= 0)
        return -1;

    *outHdc = NULL;
    *outBitmap = NULL;

    void *bits = NULL;
    HDC memDC = NULL;
    HBITMAP hBmp = create_argb_dib(&memDC, width, height, &bits);
    if (!hBmp) return -1;

    /* Create GDI+ bitmap from DIB data (non-premultiplied ARGB) */
    Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB, (BYTE *)bits);
    Graphics *g = Graphics::FromImage(&bitmap);

    g->SetSmoothingMode(SmoothingModeAntiAlias);
    g->SetTextRenderingHint(TextRenderingHintAntiAlias);

    /* Clear to transparent */
    g->Clear(Color(0, 0, 0, 0));

    switch (type) {
        case WIDGET_GAUGE:
            render_gauge(g, width, height, value, value_str, title, units, cfg);
            break;
        case WIDGET_CARD:
            render_card(g, width, height, value, value_str, title, units, cfg);
            break;
        case WIDGET_TREND:
            render_trend(g, width, height, value, value_str, title, units, cfg,
                         points, point_count);
            break;
    }

    delete g;

    /* Premultiply alpha for UpdateLayeredWindow */
    premultiply_alpha((unsigned char *)bits, width, height);

    *outHdc = memDC;
    *outBitmap = hBmp;
    return 0;
}
