#include "statistics_internal.h"
#include <stdlib.h>
#include <string.h>

static int s_summaryDateKey;

static int CurrentLocalDateKey(void) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return now.wYear * 10000 + now.wMonth * 100 + now.wDay;
}

static void BuildPath(char* output, size_t size, const char* name) {
    snprintf(output, size, "%s\\%s", g_statistics.directory, name);
}

static void EscapeJson(const char* input, char* output, size_t size) {
    size_t used = 0;
    if (!output || size == 0) return;
    for (const unsigned char* p = (const unsigned char*)input;
         p && *p && used + 2 < size; ++p) {
        if (*p == '"' || *p == '\\') output[used++] = '\\';
        if (*p >= 0x20) output[used++] = (char)*p;
    }
    output[used] = '\0';
}

BOOL Statistics_WriteSummaryExport(void) {
    if (!g_statistics.storage_available) return FALSE;
    StatisticsSummary today;
    StatisticsSummary week;
    StatisticsSummary all;
    if (!Statistics_Query(STATS_RANGE_TODAY, NULL, &today) ||
        !Statistics_Query(STATS_RANGE_SEVEN_DAYS, NULL, &week) ||
        !Statistics_Query(STATS_RANGE_ALL, NULL, &all)) return FALSE;
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"schema_version\":1,\"today\":{\"focus_seconds\":%lld,"
        "\"completed\":%d,\"cancelled\":%d},\"week\":{"
        "\"focus_seconds\":%lld,\"completed\":%d,\"cancelled\":%d},"
        "\"streak\":{\"current\":%d,\"longest\":%d},"
        "\"all\":{\"focus_seconds\":%lld,\"completed\":%d,"
        "\"cancelled\":%d,\"active_days\":%d}}\n",
        (long long)today.total_focus_seconds, today.completed_sessions,
        today.cancelled_sessions, (long long)week.total_focus_seconds,
        week.completed_sessions, week.cancelled_sessions,
        all.current_streak, all.longest_streak,
        (long long)all.total_focus_seconds, all.completed_sessions,
        all.cancelled_sessions, all.active_days);
    char path[MAX_PATH];
    BuildPath(path, sizeof(path), "summary.json");
    BOOL result = Statistics_AtomicReplace(path, json);
    if (result) s_summaryDateKey = CurrentLocalDateKey();
    return result;
}

BOOL Statistics_WriteRuntimeExport(BOOL force) {
    if (!g_statistics.storage_available) return FALSE;
    if (s_summaryDateKey != CurrentLocalDateKey()) {
        (void)Statistics_WriteSummaryExport();
    }
    int64_t mono = Statistics_MonotonicNowMs();
    if (!force && g_statistics.last_runtime_write_mono_ms > 0 &&
        mono - g_statistics.last_runtime_write_mono_ms < 1000) return TRUE;
    g_statistics.last_runtime_write_mono_ms = mono;
    if (!g_statistics.active_valid) return Statistics_ClearRuntimeExport();

    int64_t focusedMs = g_statistics.focused_mono_ms;
    int64_t pausedMs = g_statistics.paused_mono_ms;
    if (g_statistics.active_paused) {
        pausedMs += mono - g_statistics.pause_start_mono_ms;
    } else {
        focusedMs += mono - g_statistics.active_start_mono_ms;
    }
    int elapsed = (int)(focusedMs / 1000);
    int remaining = g_statistics.active.planned_seconds - elapsed;
    if (remaining < 0) remaining = 0;
    char id[STATISTICS_CATEGORY_ID_MAX * 2];
    char name[STATISTICS_CATEGORY_NAME_MAX * 2];
    char color[STATISTICS_COLOR_MAX * 2];
    EscapeJson(g_statistics.active.category_id, id, sizeof(id));
    EscapeJson(g_statistics.active.category_name, name, sizeof(name));
    EscapeJson(g_statistics.active.category_color, color, sizeof(color));
    int64_t snapshotUtc = Statistics_UtcNowMs();
    size_t capacity = 2048 + (size_t)g_statistics.active.span_count * 64;
    char* json = (char*)malloc(capacity);
    if (!json) return FALSE;
    int used = snprintf(json, capacity,
        "{\"schema_version\":1,\"mode\":\"pomodoro\","
        "\"step_kind\":\"focus\",\"status\":\"active\","
        "\"active\":true,\"running\":true,\"paused\":%s,"
        "\"session_id\":%llu,\"start_utc_ms\":%lld,"
        "\"snapshot_utc_ms\":%lld,\"planned_seconds\":%d,"
        "\"focused_seconds\":%d,\"paused_seconds\":%d,"
        "\"elapsed_seconds\":%d,\"remaining_seconds\":%d,"
        "\"cycle\":%d,\"step\":%d,\"category_id\":\"%s\","
        "\"category_name\":\"%s\",\"category_color\":\"%s\","
        "\"spans\":[",
        g_statistics.active_paused ? "true" : "false",
        (unsigned long long)g_statistics.active.id,
        (long long)g_statistics.active.start_utc_ms,
        (long long)snapshotUtc, g_statistics.active.planned_seconds,
        elapsed, (int)(pausedMs / 1000), elapsed, remaining,
        g_statistics.active.pomodoro_cycle, g_statistics.active.pomodoro_step,
        id, name, color);
    if (used < 0 || (size_t)used >= capacity) { free(json); return FALSE; }
    for (int i = 0; i < g_statistics.active.span_count; ++i) {
        const StatisticsFocusSpan* span = &g_statistics.active.spans[i];
        int64_t end = span->end_utc_ms;
        if (end == 0) end = snapshotUtc > span->start_utc_ms
            ? snapshotUtc : span->start_utc_ms;
        int written = snprintf(json + used, capacity - (size_t)used,
            "%s[%lld,%lld]", i ? "," : "",
            (long long)span->start_utc_ms, (long long)end);
        if (written < 0 || (size_t)written >= capacity - (size_t)used) {
            free(json); return FALSE;
        }
        used += written;
    }
    if ((size_t)used + 4 >= capacity) { free(json); return FALSE; }
    memcpy(json + used, "]}\n", 4);
    char path[MAX_PATH];
    BuildPath(path, sizeof(path), "runtime_state.json");
    BOOL result = Statistics_AtomicReplace(path, json);
    free(json);
    if (result) Statistics_RefreshOpenWindow();
    return result;
}
