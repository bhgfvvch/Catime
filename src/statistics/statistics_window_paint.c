#include "statistics_window_internal.h"
#include "language.h"
#include "utils/string_convert.h"
#include "color/color_parser.h"
#include <stdio.h>
#include <string.h>

static const wchar_t* TAB_LABELS[] = {
    L"Today", L"7 Days", L"Month", L"Year", L"All"
};

static int Px(UINT dpi, int value) {
    return DialogModern_Scale(dpi, value);
}

static void FormatDuration(int64_t seconds, wchar_t* output, size_t size) {
    int64_t hours = seconds / 3600;
    int minutes = (int)((seconds % 3600) / 60);
    if (hours > 0) _snwprintf_s(output, size, _TRUNCATE, L"%lldh %02dm", hours, minutes);
    else _snwprintf_s(output, size, _TRUNCATE, L"%dm", minutes);
}

static void DrawTextLine(HDC dc, HFONT font, COLORREF color, RECT rect,
                         const wchar_t* text, UINT format) {
    DialogModern_DrawText(dc, font, color, &rect, text,
                          format | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void DrawHeader(HDC dc, const RECT* client,
                       const DialogModernPalette* palette, UINT dpi) {
    HFONT title = DialogModern_CreateFont(dpi, 24, FW_SEMIBOLD);
    HFONT regular = DialogModern_CreateFont(dpi, 15, FW_MEDIUM);
    RECT titleRect = {Px(dpi, 24), Px(dpi, 8),
                      client->right - Px(dpi, 24), Px(dpi, 46)};
    DrawTextLine(dc, title, palette->text, titleRect,
        GetLocalizedString(L"统计", L"Statistics"), DT_LEFT);
    int tabWidth = client->right / 5;
    for (int i = 0; i < 5; ++i) {
        RECT tab = {i * tabWidth, Px(dpi, 48),
                    (i + 1) * tabWidth, Px(dpi, 88)};
        const wchar_t* label = GetLocalizedString(NULL, TAB_LABELS[i]);
        DrawTextLine(dc, regular,
            i == g_statisticsWindow.range ? palette->accent : palette->mutedText,
            tab, label, DT_CENTER);
        if (i == g_statisticsWindow.range) {
            RECT mark = {tab.left + Px(dpi, 16), Px(dpi, 85),
                         tab.right - Px(dpi, 16), Px(dpi, 88)};
            HBRUSH brush = CreateSolidBrush(palette->accent);
            FillRect(dc, &mark, brush);
            DeleteObject(brush);
        }
    }
    DeleteObject(title);
    DeleteObject(regular);
}

static void DrawNavigation(HDC dc, const RECT* client,
                           const DialogModernPalette* palette, UINT dpi) {
    if (g_statisticsWindow.range == STATS_RANGE_ALL) return;
    HFONT font = DialogModern_CreateFont(dpi, 16, FW_MEDIUM);
    wchar_t date[64];
    if (g_statisticsWindow.range == STATS_RANGE_YEAR) {
        _snwprintf_s(date, _countof(date), _TRUNCATE, L"%04u",
                     g_statisticsWindow.anchor.wYear);
    } else if (g_statisticsWindow.range == STATS_RANGE_MONTH) {
        _snwprintf_s(date, _countof(date), _TRUNCATE, L"%04u-%02u",
                     g_statisticsWindow.anchor.wYear,
                     g_statisticsWindow.anchor.wMonth);
    } else {
        _snwprintf_s(date, _countof(date), _TRUNCATE, L"%04u-%02u-%02u",
                     g_statisticsWindow.anchor.wYear,
                     g_statisticsWindow.anchor.wMonth,
                     g_statisticsWindow.anchor.wDay);
    }
    RECT left = {Px(dpi, 24), Px(dpi, 92), Px(dpi, 90), Px(dpi, 132)};
    RECT center = {Px(dpi, 90), Px(dpi, 92),
                   client->right - Px(dpi, 90), Px(dpi, 132)};
    RECT right = {client->right - Px(dpi, 90), Px(dpi, 92),
                  client->right - Px(dpi, 24), Px(dpi, 132)};
    DrawTextLine(dc, font, palette->text, left, L"<", DT_CENTER);
    DrawTextLine(dc, font, palette->text, center, date, DT_CENTER);
    DrawTextLine(dc, font, StatisticsWindow_CanNavigateNext()
        ? palette->text : palette->border, right, L">", DT_CENTER);
    DeleteObject(font);
}

static void DrawMetric(HDC dc, UINT dpi, const DialogModernPalette* palette,
                       RECT rect, const wchar_t* label, const wchar_t* value) {
    HFONT labelFont = DialogModern_CreateFont(dpi, 13, FW_NORMAL);
    HFONT valueFont = DialogModern_CreateFont(dpi, 20, FW_SEMIBOLD);
    RECT top = rect;
    RECT bottom = rect;
    top.bottom = top.top + Px(dpi, 24);
    bottom.top = top.bottom;
    DrawTextLine(dc, labelFont, palette->mutedText, top, label, DT_CENTER);
    DrawTextLine(dc, valueFont, palette->text, bottom, value, DT_CENTER);
    DeleteObject(labelFont);
    DeleteObject(valueFont);
}

static void DrawMetrics(HDC dc, const RECT* client,
                        const DialogModernPalette* palette, UINT dpi) {
    int width = client->right / 3;
    wchar_t duration[64];
    wchar_t completed[32];
    wchar_t third[32];
    FormatDuration(g_statisticsWindow.summary.total_focus_seconds,
                   duration, _countof(duration));
    _snwprintf_s(completed, _countof(completed), _TRUNCATE, L"%d",
                 g_statisticsWindow.summary.completed_sessions);
    if (g_statisticsWindow.range == STATS_RANGE_TODAY ||
        g_statisticsWindow.range == STATS_RANGE_SEVEN_DAYS) {
        _snwprintf_s(third, _countof(third), _TRUNCATE, L"%d",
                     g_statisticsWindow.summary.cancelled_sessions);
    } else {
        _snwprintf_s(third, _countof(third), _TRUNCATE, L"%d",
                     g_statisticsWindow.summary.active_days);
    }
    RECT metric = {0, Px(dpi, 136), width, Px(dpi, 204)};
    DrawMetric(dc, dpi, palette, metric,
        GetLocalizedString(L"专注时间", L"Total Focus"), duration);
    OffsetRect(&metric, width, 0);
    DrawMetric(dc, dpi, palette, metric,
        GetLocalizedString(L"已完成", L"Completed"), completed);
    OffsetRect(&metric, width, 0);
    DrawMetric(dc, dpi, palette, metric,
        g_statisticsWindow.range <= STATS_RANGE_SEVEN_DAYS
            ? GetLocalizedString(L"中断", L"Interrupted")
            : GetLocalizedString(L"活跃天数", L"Active Days"), third);
}

static void DrawCategories(HDC dc, const RECT* client,
                           const DialogModernPalette* palette, UINT dpi) {
    HFONT font = DialogModern_CreateFont(dpi, 14, FW_NORMAL);
    int y = client->bottom - Px(dpi, 150);
    int start = g_statisticsWindow.category_offset;
    int shown = g_statisticsWindow.summary.category_count - start;
    if (shown > 4) shown = 4;
    for (int row = 0; row < shown; ++row) {
        int i = start + row;
        StatisticsCategoryValue* item = &g_statisticsWindow.summary.categories[i];
        wchar_t name[STATISTICS_CATEGORY_NAME_MAX];
        wchar_t value[80];
        MultiByteToWideChar(CP_UTF8, 0, item->name, -1,
                            name, _countof(name));
        if (strcmp(item->id, "general") == 0) {
            wcscpy_s(name, _countof(name),
                     GetLocalizedString(L"通用", L"General"));
        }
        wchar_t duration[48];
        FormatDuration(item->focused_seconds, duration, _countof(duration));
        _snwprintf_s(value, _countof(value), _TRUNCATE, L"%s   %d%%",
                     duration, item->percentage);
        RECT nameRect = {Px(dpi, 48), y, client->right / 2, y + Px(dpi, 28)};
        RECT valueRect = {client->right / 2, y,
                          client->right - Px(dpi, 32), y + Px(dpi, 28)};
        DrawTextLine(dc, font, palette->text, nameRect, name, DT_LEFT);
        DrawTextLine(dc, font, palette->mutedText, valueRect, value, DT_RIGHT);
        RECT track = {Px(dpi, 48), y + Px(dpi, 24),
                      client->right - Px(dpi, 32), y + Px(dpi, 28)};
        HBRUSH trackBrush = CreateSolidBrush(palette->border);
        FillRect(dc, &track, trackBrush);
        DeleteObject(trackBrush);
        COLORREF categoryColor = palette->accent;
        ColorStringToColorRef(item->color, &categoryColor);
        RECT fill = track;
        fill.right = fill.left +
            (track.right - track.left) * item->percentage / 100;
        HBRUSH fillBrush = CreateSolidBrush(categoryColor);
        FillRect(dc, &fill, fillBrush);
        DeleteObject(fillBrush);
        y += Px(dpi, 30);
    }
    if (g_statisticsWindow.summary.category_count > shown) {
        wchar_t page[32];
        _snwprintf_s(page, _countof(page), _TRUNCATE, L"%d-%d / %d",
                     start + 1, start + shown,
                     g_statisticsWindow.summary.category_count);
        RECT pageRect = {client->right - Px(dpi, 130),
                         client->bottom - Px(dpi, 28),
                         client->right - Px(dpi, 32), client->bottom};
        DrawTextLine(dc, font, palette->mutedText, pageRect, page, DT_RIGHT);
    }
    DeleteObject(font);
}

static void DrawFooterInfo(HDC dc, const RECT* client,
                           const DialogModernPalette* palette, UINT dpi) {
    HFONT font = DialogModern_CreateFont(dpi, 14, FW_NORMAL);
    wchar_t line[256] = {0};
    StatisticsSummary* summary = &g_statisticsWindow.summary;
    if (g_statisticsWindow.range == STATS_RANGE_SEVEN_DAYS) {
        _snwprintf_s(line, _countof(line), _TRUNCATE, L"%s: %d",
            GetLocalizedString(L"活跃天数", L"Active Days"), summary->active_days);
    } else if (g_statisticsWindow.range == STATS_RANGE_MONTH) {
        wchar_t average[48];
        FormatDuration(summary->average_active_day_seconds, average, _countof(average));
        _snwprintf_s(line, _countof(line), _TRUNCATE,
            L"%s: %s     %s: %u-%02u-%02u     %s: %d",
            GetLocalizedString(L"活跃日均值", L"Average / active day"), average,
            GetLocalizedString(L"最佳专注日", L"Best Day"),
            summary->best_day.wYear, summary->best_day.wMonth, summary->best_day.wDay,
            GetLocalizedString(L"当前连续天数", L"Current Streak"),
            summary->current_streak);
    } else if (g_statisticsWindow.range == STATS_RANGE_YEAR) {
        int bestMonth = 0;
        int64_t bestSeconds = 0;
        int64_t months[12] = {0};
        for (int i = 0; i < summary->day_count; ++i) {
            int month = summary->days[i].date.wMonth - 1;
            months[month] += summary->days[i].focused_seconds;
        }
        for (int i = 0; i < 12; ++i) {
            if (months[i] > bestSeconds) { bestSeconds = months[i]; bestMonth = i + 1; }
        }
        _snwprintf_s(line, _countof(line), _TRUNCATE,
            L"%s: %d     %s: %02d",
            GetLocalizedString(L"最长连续天数", L"Longest Streak"),
            summary->longest_streak,
            GetLocalizedString(L"最佳月份", L"Best Month"), bestMonth);
    } else if (g_statisticsWindow.range == STATS_RANGE_ALL) {
        _snwprintf_s(line, _countof(line), _TRUNCATE,
            L"%s: %d     %s: %d",
            GetLocalizedString(L"记录天数", L"Recorded Days"), summary->active_days,
            GetLocalizedString(L"最长连续天数", L"Longest Streak"),
            summary->longest_streak);
    }
    if (line[0]) {
        RECT rect = {Px(dpi, 24), client->bottom - Px(dpi, 174),
                     client->right - Px(dpi, 24),
                     client->bottom - Px(dpi, 146)};
        DrawTextLine(dc, font, palette->mutedText, rect, line, DT_CENTER);
    }
    DeleteObject(font);
}

void StatisticsWindow_Paint(HWND hwnd, HDC target, const RECT* client) {
    int width = client->right - client->left;
    int height = client->bottom - client->top;
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    DialogModernPalette palette;
    DialogModern_ResolvePalette(&palette);
    HBRUSH background = CreateSolidBrush(palette.background);
    FillRect(memory, client, background);
    DeleteObject(background);
    SetBkMode(memory, TRANSPARENT);
    DrawHeader(memory, client, &palette, g_statisticsWindow.dpi);
    DrawNavigation(memory, client, &palette, g_statisticsWindow.dpi);
    DrawMetrics(memory, client, &palette, g_statisticsWindow.dpi);
    if (g_statisticsWindow.summary.total_focus_seconds <= 0) {
        HFONT empty = DialogModern_CreateFont(g_statisticsWindow.dpi, 16, FW_NORMAL);
        RECT area = {Px(g_statisticsWindow.dpi, 24),
                     Px(g_statisticsWindow.dpi, 240),
                     client->right - Px(g_statisticsWindow.dpi, 24),
                     client->bottom - Px(g_statisticsWindow.dpi, 40)};
        DrawTextLine(memory, empty, palette.mutedText, area,
            GetLocalizedString(L"暂无专注数据", L"No focus data yet"), DT_CENTER);
        DeleteObject(empty);
    } else {
        RECT chart = {Px(g_statisticsWindow.dpi, 48),
                      Px(g_statisticsWindow.dpi, 224),
                      client->right - Px(g_statisticsWindow.dpi, 48),
                      client->bottom - Px(g_statisticsWindow.dpi, 180)};
        if (g_statisticsWindow.range == STATS_RANGE_TODAY ||
            g_statisticsWindow.range == STATS_RANGE_ALL) {
            StatisticsWindow_DrawDonut(memory, &chart,
                                       &g_statisticsWindow.summary, &palette);
        } else {
            StatisticsWindow_DrawBars(memory, &chart,
                                      &g_statisticsWindow.summary, &palette);
        }
        DrawFooterInfo(memory, client, &palette, g_statisticsWindow.dpi);
        DrawCategories(memory, client, &palette, g_statisticsWindow.dpi);
    }
    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    (void)hwnd;
}
