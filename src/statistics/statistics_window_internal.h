#ifndef CATIME_STATISTICS_WINDOW_INTERNAL_H
#define CATIME_STATISTICS_WINDOW_INTERNAL_H

#include "statistics_internal.h"
#include "dialog/dialog_modern.h"

typedef struct {
    HWND hwnd;
    HWND owner;
    StatisticsRangeKind range;
    SYSTEMTIME anchor;
    StatisticsSummary summary;
    UINT dpi;
    int category_offset;
} StatisticsWindowState;

extern StatisticsWindowState g_statisticsWindow;

void StatisticsWindow_Paint(HWND hwnd, HDC target, const RECT* client);
void StatisticsWindow_ChangeRange(StatisticsRangeKind range);
void StatisticsWindow_Navigate(int direction);
BOOL StatisticsWindow_CanNavigateNext(void);
void StatisticsWindow_DrawDonut(HDC dc, const RECT* rect,
                                const StatisticsSummary* summary,
                                const DialogModernPalette* palette);
void StatisticsWindow_DrawBars(HDC dc, const RECT* rect,
                               const StatisticsSummary* summary,
                               const DialogModernPalette* palette);

#endif
