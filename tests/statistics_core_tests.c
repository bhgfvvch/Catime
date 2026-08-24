#include "statistics/statistics.h"
#include "statistics/statistics_navigation.h"
#include "statistics_internal.h"
#include "log.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Assertion failed: %s (%s:%d)\n", \
                #condition, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)
static int64_t s_utc;
static int64_t s_mono;
static int64_t FakeUtc(void) { return s_utc; }
static int64_t FakeMono(void) { return s_mono; }
void WriteLog(LogLevel level, const char* format, ...) {
    (void)level;
    (void)format;
}
BOOL Statistics_EnsureDirectory(void) { return TRUE; }
BOOL Statistics_LoadSessions(void) { return TRUE; }
BOOL Statistics_LoadCategories(void) { return TRUE; }
BOOL Statistics_SaveCategories(void) { return TRUE; }
BOOL Statistics_RecoverRuntime(void) { return TRUE; }
BOOL Statistics_WriteSummaryExport(void) { return TRUE; }
BOOL Statistics_WriteRuntimeExport(BOOL force) { (void)force; return TRUE; }
BOOL Statistics_ClearRuntimeExport(void) { return TRUE; }
BOOL Statistics_AppendSession(const StatisticsSession* session) {
    (void)session;
    return TRUE;
}
BOOL Statistics_SavePendingSession(const StatisticsSession* session) {
    (void)session; return TRUE;
}
BOOL Statistics_LoadPendingSession(StatisticsSession* session) {
    (void)session; return FALSE;
}
BOOL Statistics_ClearPendingSession(void) { return TRUE; }
BOOL Statistics_SessionIdOnDisk(uint64_t id) { (void)id; return FALSE; }
void Statistics_RefreshOpenWindow(void) {}
void Statistics_ShowWindow(HWND owner) { (void)owner; }
void Statistics_ShowCategoryManager(HWND owner) { (void)owner; }
int Statistics_GetCategories(StatisticsCategory* output, int capacity) {
    if (output && capacity > 0) *output = g_statistics.categories[0];
    return 1;
}
BOOL Statistics_GetSelectedCategory(StatisticsCategory* output) {
    if (!output) return FALSE;
    *output = g_statistics.categories[0];
    return TRUE;
}
BOOL Statistics_SelectCategory(const char* id) { (void)id; return TRUE; }
BOOL Statistics_AddCategory(const char* name, const char* color) {
    (void)name; (void)color; return TRUE;
}
BOOL Statistics_RenameCategory(const char* id, const char* name) {
    (void)id; (void)name; return TRUE;
}
BOOL Statistics_DeleteCategory(const char* id) { (void)id; return TRUE; }
BOOL Statistics_SetCategoryColor(const char* id, const char* color) {
    (void)id; (void)color; return TRUE;
}
BOOL Statistics_MoveCategory(const char* id, int direction) {
    (void)id; (void)direction; return TRUE;
}
static void ResetState(void) {
    Statistics_FreeSessions();
    free(g_statistics.active.spans);
    memset(&g_statistics, 0, sizeof(g_statistics));
    strcpy_s(g_statistics.directory, sizeof(g_statistics.directory), "test");
    g_statistics.initialized = TRUE;
    g_statistics.storage_available = TRUE;
    strcpy_s(g_statistics.categories[0].id,
             sizeof(g_statistics.categories[0].id), "general");
    strcpy_s(g_statistics.categories[0].name,
             sizeof(g_statistics.categories[0].name), "General");
    strcpy_s(g_statistics.categories[0].color,
             sizeof(g_statistics.categories[0].color), "#3A96DD");
    g_statistics.categories[0].enabled = TRUE;
    g_statistics.category_count = 1;
    g_statistics.next_id = 1;
    s_utc = 1000000000;
    s_mono = 5000;
    Statistics_SetClocksForTesting(FakeUtc, FakeMono);
}
static void Advance(int seconds) {
    s_utc += (int64_t)seconds * 1000;
    s_mono += (int64_t)seconds * 1000;
}
static void TestLifecycle(void) {
    ResetState();
    Statistics_OnFocusStepStarted(1500, 1, 1);
    Advance(600);
    Statistics_OnPause();
    Advance(300);
    Statistics_OnResume();
    Advance(900);
    Statistics_OnFocusStepCompleted();
    assert(g_statistics.session_count == 1);
    StatisticsSession* session = &g_statistics.sessions[0];
    assert(session->focused_seconds == 1500);
    assert(session->paused_seconds == 300);
    assert(session->status == STATS_SESSION_COMPLETED);
    assert(session->span_count == 2);
    Statistics_OnFocusStepCancelled();
    assert(g_statistics.session_count == 1);
    Statistics_OnFocusStepStarted(1500, 1, 1);
    Advance(600);
    Statistics_OnFocusStepCancelled();
    Statistics_OnFocusStepCancelled();
    assert(g_statistics.session_count == 2);
    assert(g_statistics.sessions[1].status == STATS_SESSION_CANCELLED);
    assert(g_statistics.sessions[1].focused_seconds == 600);
}
static void TestActiveQuery(void) {
    ResetState();
    Statistics_OnFocusStepStarted(300, 1, 1); Advance(7);
    StatisticsSummary summary;
    assert(Statistics_Query(STATS_RANGE_ALL, NULL, &summary));
    assert(summary.total_focus_seconds == 7 && summary.completed_sessions == 0 && summary.cancelled_sessions == 0);
    Statistics_OnFocusStepCancelled();
}
static StatisticsSession MakeSession(SYSTEMTIME startDate, int startMinute,
                                     int durationMinutes, const char* category,
                                     StatisticsSessionStatus status) {
    StatisticsSession session = {0};
    startDate.wHour = (WORD)(startMinute / 60);
    startDate.wMinute = (WORD)(startMinute % 60);
    int64_t startMidnight = Statistics_LocalDateToUtcMs(&startDate);
    session.id = g_statistics.next_id++;
    session.start_utc_ms = startMidnight + (int64_t)startMinute * 60000;
    session.end_utc_ms = session.start_utc_ms + (int64_t)durationMinutes * 60000;
    session.focused_seconds = durationMinutes * 60;
    session.planned_seconds = session.focused_seconds;
    session.status = status;
    strcpy_s(session.category_id, sizeof(session.category_id), category);
    strcpy_s(session.category_name, sizeof(session.category_name), category);
    strcpy_s(session.category_color, sizeof(session.category_color), "#57A773");
    session.span_count = 1;
    session.span_capacity = 1;
    session.spans = (StatisticsFocusSpan*)calloc(1, sizeof(*session.spans));
    assert(session.spans);
    session.spans[0].start_utc_ms = session.start_utc_ms;
    session.spans[0].end_utc_ms = session.end_utc_ms;
    return session;
}
static void TestAggregation(void) {
    ResetState();
    SYSTEMTIME day = {0};
    day.wYear = 2026; day.wMonth = 8; day.wDay = 23;
    StatisticsSession first = MakeSession(day, 23 * 60 + 50, 25, "coding", STATS_SESSION_COMPLETED);
    first.spans = (StatisticsFocusSpan*)realloc(first.spans, 2 * sizeof(*first.spans));
    assert(first.spans); first.span_count = first.span_capacity = 2;
    first.spans[0].end_utc_ms = first.start_utc_ms + 5 * 60000LL;
    first.spans[1].start_utc_ms = first.start_utc_ms + 25 * 60000LL;
    first.spans[1].end_utc_ms = first.spans[1].start_utc_ms + 5 * 60000LL;
    first.end_utc_ms = first.spans[1].end_utc_ms; first.focused_seconds = 600; first.paused_seconds = 1200;
    assert(Statistics_AddLoadedSession(&first)); free(first.spans);
    StatisticsSummary summary;
    assert(Statistics_Query(STATS_RANGE_TODAY, &day, &summary));
    assert(summary.total_focus_seconds == 300);
    assert(summary.completed_sessions == 0);
    Statistics_AddDays(&day, 1);
    assert(Statistics_Query(STATS_RANGE_TODAY, &day, &summary));
    assert(summary.total_focus_seconds == 300);
    assert(summary.completed_sessions == 1);
    StatisticsSession second = MakeSession(day, 10 * 60, 30,
                                           "reading", STATS_SESSION_CANCELLED);
    assert(Statistics_AddLoadedSession(&second)); free(second.spans);
    assert(Statistics_Query(STATS_RANGE_SEVEN_DAYS, &day, &summary));
    assert(summary.total_focus_seconds == 2100);
    assert(summary.completed_sessions == 1 && summary.cancelled_sessions == 1);
    assert(summary.category_count == 2 && summary.active_days == 1);
    assert(summary.longest_streak == 2);
}
static void TestParsingAndStepKind(void) {
    assert(Pomodoro_GetStepKind(0) == POMODORO_STEP_FOCUS);
    assert(Pomodoro_GetStepKind(1) == POMODORO_STEP_BREAK);
    assert(Pomodoro_GetStepKind(6) == POMODORO_STEP_FOCUS);
    StatisticsSession session;
    assert(!Statistics_ParseSessionLine("not json", &session));
    const char* line = "{\"schema_version\":1,\"id\":9,\"start_utc_ms\":1000,"
        "\"end_utc_ms\":2000,\"planned_seconds\":1,\"focused_seconds\":1,"
        "\"paused_seconds\":0,\"status\":\"completed\","
        "\"category_id\":\"study\",\"category_name\":\"学习\","
        "\"category_color\":\"#3A96DD\",\"cycle\":1,\"step\":1,"
        "\"spans\":[[1000,2000]]}";
    assert(Statistics_ParseSessionLine(line, &session));
    assert(strcmp(session.category_name, "学习") == 0);
    free(session.spans);
}
static void TestRangesAndStreak(void) {
    ResetState();
    SYSTEMTIME today;
    GetLocalTime(&today);
    today.wHour = today.wMinute = today.wSecond = today.wMilliseconds = 0;
    SYSTEMTIME date = today;
    Statistics_AddDays(&date, -2);
    for (int i = 0; i < 3; ++i) {
        StatisticsSession session = MakeSession(date, 9 * 60, 25,
            i == 1 ? "历史分类" : "general", STATS_SESSION_COMPLETED);
        assert(Statistics_AddLoadedSession(&session)); free(session.spans);
        Statistics_AddDays(&date, 1);
    }
    StatisticsSummary summary;
    assert(Statistics_Query(STATS_RANGE_ALL, NULL, &summary));
    assert(summary.current_streak == 3);
    assert(summary.longest_streak == 3);
    assert(summary.category_count == 2);
    assert(summary.active_days == 3);
    ResetState();
    SYSTEMTIME fixed = {0};
    fixed.wYear = 2024; fixed.wMonth = 6; fixed.wDay = 15;
    date = fixed;
    for (int i = 0; i < 3; ++i) {
        StatisticsSession session = MakeSession(date, 9 * 60, 25,
            "general", STATS_SESSION_COMPLETED);
        assert(Statistics_AddLoadedSession(&session));
        free(session.spans);
        Statistics_AddDays(&date, 1);
    }
    assert(Statistics_Query(STATS_RANGE_MONTH, &fixed, &summary));
    assert(summary.total_focus_seconds == 4500);
    assert(summary.completed_sessions == 3);
    assert(Statistics_Query(STATS_RANGE_YEAR, &fixed, &summary));
    assert(summary.total_focus_seconds == 4500);
    assert(summary.day_count >= 365);
    assert(Statistics_Query(STATS_RANGE_ALL, NULL, &summary));
    assert(summary.active_days == 3);
    ResetState();
    date = fixed;
    for (int i = 0; i < 40; ++i) {
        char category[32];
        snprintf(category, sizeof(category), "historical_%d", i);
        StatisticsSession session = MakeSession(date, 9 * 60, 1,
            category, STATS_SESSION_COMPLETED);
        assert(Statistics_AddLoadedSession(&session));
        free(session.spans);
        Statistics_AddDays(&date, 1);
    }
    assert(Statistics_Query(STATS_RANGE_ALL, NULL, &summary));
    assert(summary.category_count == STATISTICS_MAX_CATEGORIES);
    int64_t categoryTotal = 0;
    for (int i = 0; i < summary.category_count; ++i) categoryTotal += summary.categories[i].focused_seconds;
    assert(categoryTotal == summary.total_focus_seconds);
}
static void TestLargeHistory(void) {
    ResetState();
    SYSTEMTIME day = {0}; day.wYear = 2026; day.wMonth = 1; day.wDay = 1;
    StatisticsSession sample = MakeSession(day, 8 * 60, 1, "general", STATS_SESSION_COMPLETED);
    for (int i = 0; i < 50000; ++i) {
        sample.id = (uint64_t)i + 1;
        SYSTEMTIME current = day;
        Statistics_AddDays(&current, i % 5000);
        int64_t timeOfDay = i % 97 == 0 ? 86370000LL : 8 * 3600000LL;
        sample.start_utc_ms = Statistics_LocalDateToUtcMs(&current) + timeOfDay;
        sample.end_utc_ms = sample.start_utc_ms + 60000;
        sample.spans[0].start_utc_ms = sample.start_utc_ms;
        sample.spans[0].end_utc_ms = sample.end_utc_ms;
        snprintf(sample.category_id, sizeof(sample.category_id), "history_%d", i % 40);
        strcpy_s(sample.category_name, sizeof(sample.category_name),
                 sample.category_id);
        assert(Statistics_AddLoadedSession(&sample));
    }
    free(sample.spans); StatisticsSummary summary;
    ULONGLONG queryStart = GetTickCount64();
    assert(Statistics_Query(STATS_RANGE_ALL, NULL, &summary) && GetTickCount64() - queryStart < 15000);
    assert(summary.total_focus_seconds == 3000000);
    assert(summary.completed_sessions == 50000);
    assert(summary.active_days > 1000);
}
static void TestNavigation(void) {
    SYSTEMTIME today = {0};
    today.wYear = 2026; today.wMonth = 8; today.wDay = 24;
    SYSTEMTIME anchor = today;
    assert(!StatisticsNavigation_CanMoveNext(STATS_RANGE_TODAY,
                                             &anchor, &today));
    StatisticsNavigation_Move(STATS_RANGE_TODAY, &anchor, -1);
    assert(anchor.wDay == 23);
    assert(StatisticsNavigation_CanMoveNext(STATS_RANGE_TODAY,
                                            &anchor, &today));
    anchor.wYear = 2026; anchor.wMonth = 1; anchor.wDay = 1;
    StatisticsNavigation_Move(STATS_RANGE_MONTH, &anchor, -1);
    assert(anchor.wYear == 2025 && anchor.wMonth == 12);
    StatisticsNavigation_Move(STATS_RANGE_YEAR, &anchor, 1);
    assert(anchor.wYear == 2026 && anchor.wMonth == 1);
}
int main(void) {
    TestLifecycle(); TestActiveQuery(); TestAggregation(); TestParsingAndStepKind();
    TestRangesAndStreak(); TestLargeHistory(); TestNavigation();
    ResetState(); Statistics_FreeSessions();
    puts("statistics_core_tests: PASS");
    return 0;
}
