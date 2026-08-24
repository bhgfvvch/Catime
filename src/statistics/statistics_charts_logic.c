#include "statistics_window_internal.h"

int StatisticsCharts_DaySlot(StatisticsRangeKind range,
                             const StatisticsDayValue* day, int dayIndex,
                             int slotCount) {
    if (!day || slotCount <= 0) return -1;
    int index = range == STATS_RANGE_YEAR
        ? (int)day->date.wMonth - 1 : dayIndex;
    return index >= 0 && index < slotCount ? index : -1;
}
