#include "statistics_window_internal.h"
#include "color/color_parser.h"
#include "language.h"
#include <math.h>

static COLORREF CategoryColor(const StatisticsCategoryValue* category,
                              COLORREF fallback) {
    COLORREF color;
    return ColorStringToColorRef(category->color, &color) ? color : fallback;
}

void StatisticsWindow_DrawDonut(HDC dc, const RECT* rect,
                                const StatisticsSummary* summary,
                                const DialogModernPalette* palette) {
    int width = rect->right - rect->left;
    int height = rect->bottom - rect->top;
    int diameter = width < height ? width : height;
    RECT circle = {
        rect->left + (width - diameter) / 2,
        rect->top + (height - diameter) / 2,
        rect->left + (width + diameter) / 2,
        rect->top + (height + diameter) / 2
    };
    int cx = (circle.left + circle.right) / 2;
    int cy = (circle.top + circle.bottom) / 2;
    if (summary->total_focus_seconds <= 0 || summary->category_count == 0) {
        HBRUSH empty = CreateSolidBrush(palette->border);
        HGDIOBJ oldBrush = SelectObject(dc, empty);
        Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
        SelectObject(dc, oldBrush);
        DeleteObject(empty);
    } else if (summary->category_count == 1) {
        HBRUSH brush = CreateSolidBrush(CategoryColor(
            &summary->categories[0], palette->accent));
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    } else {
        double angle = -1.5707963267948966;
        int radius = diameter / 2;
        for (int i = 0; i < summary->category_count; ++i) {
            double sweep = 6.283185307179586 *
                (double)summary->categories[i].focused_seconds /
                (double)summary->total_focus_seconds;
            POINT start = {cx + (int)(cos(angle) * radius),
                           cy + (int)(sin(angle) * radius)};
            POINT end = {cx + (int)(cos(angle + sweep) * radius),
                         cy + (int)(sin(angle + sweep) * radius)};
            HBRUSH brush = CreateSolidBrush(CategoryColor(
                &summary->categories[i], palette->accent));
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            Pie(dc, circle.left, circle.top, circle.right, circle.bottom,
                start.x, start.y, end.x, end.y);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
            angle += sweep;
        }
    }
    int hole = diameter * 58 / 100;
    HBRUSH center = CreateSolidBrush(palette->background);
    HGDIOBJ oldBrush = SelectObject(dc, center);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, (circle.left + circle.right - hole) / 2,
            (circle.top + circle.bottom - hole) / 2,
            (circle.left + circle.right + hole) / 2,
            (circle.top + circle.bottom + hole) / 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(center);
    wchar_t duration[48];
    int64_t hours = summary->total_focus_seconds / 3600;
    int minutes = (int)((summary->total_focus_seconds % 3600) / 60);
    if (hours > 0) {
        _snwprintf_s(duration, _countof(duration), _TRUNCATE,
                     L"%lldh %02dm", hours, minutes);
    } else {
        _snwprintf_s(duration, _countof(duration), _TRUNCATE,
                     L"%dm", minutes);
    }
    HFONT valueFont = DialogModern_CreateFont(
        g_statisticsWindow.dpi, 19, FW_SEMIBOLD);
    HFONT labelFont = DialogModern_CreateFont(
        g_statisticsWindow.dpi, 11, FW_NORMAL);
    RECT valueRect = {circle.left, cy - DialogModern_Scale(
        g_statisticsWindow.dpi, 24), circle.right, cy + 2};
    RECT labelRect = {circle.left, cy + 2, circle.right,
                      cy + DialogModern_Scale(g_statisticsWindow.dpi, 28)};
    DialogModern_DrawText(dc, valueFont, palette->text, &valueRect,
                          duration, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DialogModern_DrawText(dc, labelFont, palette->mutedText, &labelRect,
        GetLocalizedString(L"专注时间", L"Total Focus"),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(valueFont);
    DeleteObject(labelFont);
}

void StatisticsWindow_DrawBars(HDC dc, const RECT* rect,
                               const StatisticsSummary* summary,
                               const DialogModernPalette* palette) {
    if (summary->day_count <= 0) return;
    int dayCount = summary->day_count;
    if (dayCount > STATISTICS_MAX_DAYS) dayCount = STATISTICS_MAX_DAYS;
    int categoryCount = summary->category_count;
    if (categoryCount < 0) categoryCount = 0;
    if (categoryCount > STATISTICS_MAX_CATEGORIES)
        categoryCount = STATISTICS_MAX_CATEGORIES;
    int count = g_statisticsWindow.range == STATS_RANGE_YEAR
        ? 12 : dayCount;
    int64_t totals[STATISTICS_MAX_DAYS] = {0};
    int64_t stacks[STATISTICS_MAX_DAYS][STATISTICS_MAX_CATEGORIES] = {{0}};
    for (int day = 0; day < dayCount; ++day) {
        int slotIndex = StatisticsCharts_DaySlot(g_statisticsWindow.range,
            &summary->days[day], day, count);
        if (slotIndex < 0) continue;
        totals[slotIndex] += summary->days[day].focused_seconds;
        for (int category = 0; category < categoryCount; ++category) {
            stacks[slotIndex][category] +=
                summary->day_category_seconds[day][category];
        }
    }
    int64_t maximum = 1;
    for (int i = 0; i < count; ++i) if (totals[i] > maximum) maximum = totals[i];
    HPEN axis = CreatePen(PS_SOLID, 1, palette->border);
    HGDIOBJ oldPen = SelectObject(dc, axis);
    int chartBottom = rect->bottom -
        DialogModern_Scale(g_statisticsWindow.dpi, 22);
    MoveToEx(dc, rect->left, chartBottom, NULL);
    LineTo(dc, rect->right, chartBottom);
    int available = rect->right - rect->left;
    int slot = available / count;
    if (slot < 2) slot = 2;
    int barWidth = slot * 2 / 3;
    if (barWidth < 2) barWidth = 2;
    for (int day = 0; day < count; ++day) {
        int x = rect->left + day * slot + (slot - barWidth) / 2;
        int bottom = chartBottom;
        for (int category = 0; category < categoryCount; ++category) {
            int64_t seconds = stacks[day][category];
            if (seconds <= 0) continue;
            int height = (int)(seconds * (chartBottom - rect->top) / maximum);
            if (height < 1) height = 1;
            RECT segment = {x, bottom - height, x + barWidth, bottom};
            HBRUSH brush = CreateSolidBrush(CategoryColor(
                &summary->categories[category], palette->accent));
            FillRect(dc, &segment, brush);
            DeleteObject(brush);
            bottom -= height;
        }
    }
    HFONT labelFont = DialogModern_CreateFont(
        g_statisticsWindow.dpi, 11, FW_NORMAL);
    HGDIOBJ oldFont = SelectObject(dc, labelFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, palette->mutedText);
    for (int item = 0; item < count; ++item) {
        if (g_statisticsWindow.range == STATS_RANGE_MONTH &&
            count > 20 && item % 2 != 0) continue;
        wchar_t label[8];
        int number = g_statisticsWindow.range == STATS_RANGE_YEAR
            ? item + 1 : summary->days[item].date.wDay;
        _snwprintf_s(label, _countof(label), _TRUNCATE, L"%d", number);
        RECT labelRect = {rect->left + item * slot, chartBottom,
                          rect->left + (item + 1) * slot, rect->bottom};
        DrawTextW(dc, label, -1, &labelRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, oldFont);
    DeleteObject(labelFont);
    SelectObject(dc, oldPen);
    DeleteObject(axis);
}
