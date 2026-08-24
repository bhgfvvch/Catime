#include "statistics_window_internal.h"
#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed: %s (%s:%d)\n", \
            #condition, __FILE__, __LINE__); return 1; } } while (0)

int main(void) {
    StatisticsDayValue day = {0};
    day.date.wMonth = 0;
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_YEAR, &day, 0, 12) == -1);
    day.date.wMonth = 1;
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_YEAR, &day, 0, 12) == 0);
    day.date.wMonth = 12;
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_YEAR, &day, 0, 12) == 11);
    day.date.wMonth = 13;
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_YEAR, &day, 0, 12) == -1);
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_MONTH, &day, -1, 31) == -1);
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_MONTH, &day, 30, 31) == 30);
    CHECK(StatisticsCharts_DaySlot(STATS_RANGE_MONTH, &day, 31, 31) == -1);
    puts("statistics_charts_tests: PASS");
    return 0;
}
