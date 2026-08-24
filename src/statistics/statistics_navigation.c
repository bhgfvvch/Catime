#include "statistics/statistics_navigation.h"
#include "statistics_internal.h"

void StatisticsNavigation_Move(StatisticsRangeKind range,
                               SYSTEMTIME* anchor, int direction) {
    if (!anchor || direction == 0 || range == STATS_RANGE_ALL) return;
    if (range == STATS_RANGE_TODAY) {
        Statistics_AddDays(anchor, direction);
    } else if (range == STATS_RANGE_SEVEN_DAYS) {
        Statistics_AddDays(anchor, direction * 7);
    } else if (range == STATS_RANGE_MONTH) {
        int month = anchor->wMonth + direction;
        if (month < 1) { month = 12; anchor->wYear--; }
        if (month > 12) { month = 1; anchor->wYear++; }
        anchor->wMonth = (WORD)month;
        anchor->wDay = 1;
    } else if (range == STATS_RANGE_YEAR) {
        anchor->wYear = (WORD)(anchor->wYear + direction);
        anchor->wMonth = 1;
        anchor->wDay = 1;
    }
}

BOOL StatisticsNavigation_CanMoveNext(StatisticsRangeKind range,
                                      const SYSTEMTIME* anchor,
                                      const SYSTEMTIME* today) {
    if (!anchor || !today || range == STATS_RANGE_ALL) return FALSE;
    int64_t start;
    int64_t end;
    if (!Statistics_DateRange(range, anchor, &start, &end)) return FALSE;
    (void)start;
    return end <= Statistics_LocalDateToUtcMs(today);
}
