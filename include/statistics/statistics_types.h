#ifndef CATIME_STATISTICS_TYPES_H
#define CATIME_STATISTICS_TYPES_H

#include <stdint.h>
#include <windows.h>

#define STATISTICS_SCHEMA_VERSION 1
#define STATISTICS_CATEGORY_ID_MAX 48
#define STATISTICS_CATEGORY_NAME_MAX 96
#define STATISTICS_COLOR_MAX 16
#define STATISTICS_MAX_CATEGORIES 32
#define STATISTICS_MAX_DAYS 366

typedef enum {
    STATS_SESSION_COMPLETED,
    STATS_SESSION_CANCELLED
} StatisticsSessionStatus;

typedef enum {
    POMODORO_STEP_FOCUS,
    POMODORO_STEP_BREAK,
    POMODORO_STEP_INVALID
} PomodoroStepKind;

typedef enum {
    STATS_RANGE_TODAY,
    STATS_RANGE_SEVEN_DAYS,
    STATS_RANGE_MONTH,
    STATS_RANGE_YEAR,
    STATS_RANGE_ALL
} StatisticsRangeKind;

typedef struct {
    int64_t start_utc_ms;
    int64_t end_utc_ms;
} StatisticsFocusSpan;

typedef struct {
    uint64_t id;
    int64_t start_utc_ms;
    int64_t end_utc_ms;
    int planned_seconds;
    int focused_seconds;
    int paused_seconds;
    int pomodoro_cycle;
    int pomodoro_step;
    StatisticsSessionStatus status;
    char category_id[STATISTICS_CATEGORY_ID_MAX];
    char category_name[STATISTICS_CATEGORY_NAME_MAX];
    char category_color[STATISTICS_COLOR_MAX];
    int span_count;
    int span_capacity;
    StatisticsFocusSpan* spans;
} StatisticsSession;

typedef struct {
    char id[STATISTICS_CATEGORY_ID_MAX];
    char name[STATISTICS_CATEGORY_NAME_MAX];
    char color[STATISTICS_COLOR_MAX];
    BOOL enabled;
    int order;
} StatisticsCategory;

typedef struct {
    SYSTEMTIME date;
    int64_t focused_seconds;
} StatisticsDayValue;

typedef struct {
    char id[STATISTICS_CATEGORY_ID_MAX];
    char name[STATISTICS_CATEGORY_NAME_MAX];
    char color[STATISTICS_COLOR_MAX];
    int64_t focused_seconds;
    int percentage;
} StatisticsCategoryValue;

typedef struct {
    int64_t total_focus_seconds;
    int completed_sessions;
    int cancelled_sessions;
    int active_days;
    int current_streak;
    int longest_streak;
    int64_t best_day_seconds;
    SYSTEMTIME best_day;
    int64_t average_active_day_seconds;
    int day_count;
    StatisticsDayValue days[STATISTICS_MAX_DAYS];
    int64_t day_category_seconds[STATISTICS_MAX_DAYS][STATISTICS_MAX_CATEGORIES];
    int category_count;
    StatisticsCategoryValue categories[STATISTICS_MAX_CATEGORIES];
} StatisticsSummary;

#endif
