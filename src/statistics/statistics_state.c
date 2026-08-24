#include "statistics_internal.h"
#include "config.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

#define WINDOWS_EPOCH_100NS 116444736000000000LL

StatisticsState g_statistics = {0};
static StatisticsClockFn s_utcClock;
static StatisticsClockFn s_monotonicClock;

static int64_t SystemUtcNowMs(void) {
    FILETIME fileTime;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return ((int64_t)value.QuadPart - WINDOWS_EPOCH_100NS) / 10000;
}

static int64_t SystemMonotonicNowMs(void) {
    return (int64_t)GetTickCount64();
}

int64_t Statistics_UtcNowMs(void) {
    return s_utcClock ? s_utcClock() : SystemUtcNowMs();
}

int64_t Statistics_MonotonicNowMs(void) {
    return s_monotonicClock ? s_monotonicClock() : SystemMonotonicNowMs();
}

void Statistics_SetClocksForTesting(StatisticsClockFn utcClock,
                                    StatisticsClockFn monotonicClock) {
    s_utcClock = utcClock;
    s_monotonicClock = monotonicClock;
}

BOOL Statistics_AddLoadedSession(const StatisticsSession* session) {
    if (!session) return FALSE;
    if (g_statistics.session_count == g_statistics.session_capacity) {
        size_t capacity = g_statistics.session_capacity
            ? g_statistics.session_capacity * 2 : 256;
        StatisticsSession* resized = (StatisticsSession*)realloc(
            g_statistics.sessions, capacity * sizeof(*resized));
        if (!resized) return FALSE;
        g_statistics.sessions = resized;
        g_statistics.session_capacity = capacity;
    }
    StatisticsSession copy = *session;
    copy.spans = NULL;
    copy.span_capacity = session->span_count;
    if (session->span_count > 0) {
        copy.spans = (StatisticsFocusSpan*)malloc(
            (size_t)session->span_count * sizeof(*copy.spans));
        if (!copy.spans) return FALSE;
        memcpy(copy.spans, session->spans,
               (size_t)session->span_count * sizeof(*copy.spans));
    }
    g_statistics.sessions[g_statistics.session_count++] = copy;
    if (session->id >= g_statistics.next_id) {
        g_statistics.next_id = session->id + 1;
    }
    return TRUE;
}

void Statistics_FreeSessions(void) {
    for (size_t i = 0; i < g_statistics.session_count; ++i) {
        free(g_statistics.sessions[i].spans);
    }
    free(g_statistics.sessions);
    g_statistics.sessions = NULL;
    g_statistics.session_count = 0;
    g_statistics.session_capacity = 0;
}

BOOL Statistics_Initialize(void) {
    if (g_statistics.initialized) return g_statistics.storage_available;
    memset(&g_statistics, 0, sizeof(g_statistics));
    g_statistics.selected_category = 0;
    g_statistics.next_id = 1;
    if (!Statistics_EnsureDirectory()) {
        LOG_WARNING("Statistics directory unavailable; statistics disabled");
        g_statistics.storage_available = FALSE;
        return FALSE;
    }
    g_statistics.storage_available = TRUE;
    if (!Statistics_LoadCategories()) {
        LOG_WARNING("Statistics categories damaged; using General");
        (void)Statistics_SaveCategories();
    }
    if (!Statistics_LoadSessions()) {
        LOG_WARNING("Statistics history could not be fully loaded");
    }
    g_statistics.initialized = TRUE;
    StatisticsSession pending = {0};
    if (Statistics_LoadPendingSession(&pending)) {
        g_statistics.pending = pending;
        g_statistics.pending_valid = TRUE;
        if (!Statistics_FlushPending()) {
            LOG_WARNING("Pending statistics session could not be recovered");
        }
    }
    if (!g_statistics.pending_valid && !Statistics_RecoverRuntime()) {
        LOG_WARNING("Runtime statistics recovery remains pending");
    }
    (void)Statistics_WriteSummaryExport();
    if (!g_statistics.pending_valid) (void)Statistics_WriteRuntimeExport(TRUE);
    return TRUE;
}

void Statistics_CloseActiveSpan(int64_t utc_ms) {
    StatisticsSession* session = &g_statistics.active;
    if (!g_statistics.active_valid || session->span_count <= 0) return;
    StatisticsFocusSpan* span = &session->spans[session->span_count - 1];
    if (span->end_utc_ms == 0) {
        span->end_utc_ms = utc_ms > span->start_utc_ms
            ? utc_ms : span->start_utc_ms;
    }
}

static void OpenActiveSpan(int64_t utc_ms) {
    StatisticsSession* session = &g_statistics.active;
    if (session->span_count == session->span_capacity) {
        int capacity = session->span_capacity ? session->span_capacity * 2 : 4;
        StatisticsFocusSpan* resized = (StatisticsFocusSpan*)realloc(
            session->spans, (size_t)capacity * sizeof(*resized));
        if (!resized) {
            LOG_WARNING("Statistics could not allocate focus span");
            return;
        }
        session->spans = resized;
        session->span_capacity = capacity;
    }
    StatisticsFocusSpan* span = &session->spans[session->span_count++];
    span->start_utc_ms = utc_ms;
    span->end_utc_ms = 0;
}

void Statistics_OnFocusStepStarted(int plannedSeconds, int cycle, int step) {
    if ((!g_statistics.initialized && !Statistics_Initialize()) ||
        !g_statistics.storage_available) return;
    if (g_statistics.pending_valid && !Statistics_FlushPending()) return;
    if (g_statistics.active_valid) Statistics_Finalize(STATS_SESSION_CANCELLED);
    memset(&g_statistics.active, 0, sizeof(g_statistics.active));
    int64_t utc = Statistics_UtcNowMs();
    int64_t mono = Statistics_MonotonicNowMs();
    StatisticsSession* session = &g_statistics.active;
    uint64_t candidate = utc > 0 ? ((uint64_t)utc << 12) : 1;
    if (candidate > g_statistics.next_id) g_statistics.next_id = candidate;
    session->id = g_statistics.next_id++;
    session->start_utc_ms = utc;
    session->planned_seconds = plannedSeconds > 0 ? plannedSeconds : 0;
    session->pomodoro_cycle = cycle;
    session->pomodoro_step = step;
    StatisticsCategory category = {0};
    if (!Statistics_GetSelectedCategory(&category)) {
        strcpy_s(category.id, sizeof(category.id), "general");
        strcpy_s(category.name, sizeof(category.name), "General");
        strcpy_s(category.color, sizeof(category.color), "#3A96DD");
    }
    strncpy_s(session->category_id, sizeof(session->category_id),
              category.id, _TRUNCATE);
    strncpy_s(session->category_name, sizeof(session->category_name),
              category.name, _TRUNCATE);
    strncpy_s(session->category_color, sizeof(session->category_color),
              category.color, _TRUNCATE);
    g_statistics.active_valid = TRUE;
    g_statistics.active_paused = FALSE;
    g_statistics.active_start_mono_ms = mono;
    g_statistics.focused_mono_ms = 0;
    g_statistics.paused_mono_ms = 0;
    OpenActiveSpan(utc);
    Statistics_UpdateRuntimeExport(TRUE);
}

void Statistics_OnPause(void) {
    if (!g_statistics.active_valid || g_statistics.active_paused) return;
    int64_t mono = Statistics_MonotonicNowMs();
    g_statistics.focused_mono_ms += mono - g_statistics.active_start_mono_ms;
    g_statistics.pause_start_mono_ms = mono;
    g_statistics.active_paused = TRUE;
    Statistics_CloseActiveSpan(Statistics_UtcNowMs());
    Statistics_UpdateRuntimeExport(TRUE);
}

void Statistics_OnResume(void) {
    if (!g_statistics.active_valid || !g_statistics.active_paused) return;
    int64_t mono = Statistics_MonotonicNowMs();
    g_statistics.paused_mono_ms += mono - g_statistics.pause_start_mono_ms;
    g_statistics.active_start_mono_ms = mono;
    g_statistics.active_paused = FALSE;
    OpenActiveSpan(Statistics_UtcNowMs());
    Statistics_UpdateRuntimeExport(TRUE);
}

void Statistics_Finalize(StatisticsSessionStatus status) {
    if (!g_statistics.active_valid) return;
    int64_t utc = Statistics_UtcNowMs();
    int64_t mono = Statistics_MonotonicNowMs();
    StatisticsSession* session = &g_statistics.active;
    if (g_statistics.active_paused) {
        g_statistics.paused_mono_ms += mono - g_statistics.pause_start_mono_ms;
    } else {
        g_statistics.focused_mono_ms += mono - g_statistics.active_start_mono_ms;
        Statistics_CloseActiveSpan(utc);
    }
    session->end_utc_ms = utc;
    session->focused_seconds = (int)(g_statistics.focused_mono_ms / 1000);
    session->paused_seconds = (int)(g_statistics.paused_mono_ms / 1000);
    session->status = status;
    g_statistics.active_valid = FALSE;
    g_statistics.active_paused = FALSE;
    if (session->focused_seconds <= 0) {
        (void)Statistics_ClearRuntimeExport();
        free(session->spans);
        memset(session, 0, sizeof(*session));
        return;
    }
    if (g_statistics.pending_valid && !Statistics_FlushPending()) {
        LOG_WARNING("Statistics pending slot is busy; runtime snapshot retained");
        return;
    }
    g_statistics.pending = *session;
    g_statistics.pending_valid = TRUE;
    memset(session, 0, sizeof(*session));
    if (!Statistics_FlushPending()) {
        LOG_WARNING("Failed to persist statistics session %llu; retry pending",
                    (unsigned long long)g_statistics.pending.id);
    }
}

static BOOL SessionInMemory(uint64_t id) {
    for (size_t i = 0; i < g_statistics.session_count; ++i) {
        if (g_statistics.sessions[i].id == id) return TRUE;
    }
    return FALSE;
}

BOOL Statistics_FlushPending(void) {
    if (!g_statistics.pending_valid) return TRUE;
    StatisticsSession* pending = &g_statistics.pending;
    BOOL inMemory = SessionInMemory(pending->id);
    BOOL onDisk = inMemory || Statistics_SessionIdOnDisk(pending->id);
    if (!onDisk) {
        if (!Statistics_SavePendingSession(pending) ||
            !Statistics_AppendSession(pending)) return FALSE;
        onDisk = TRUE;
    }
    if (!inMemory && onDisk && !Statistics_AddLoadedSession(pending)) {
        return FALSE;
    }
    (void)Statistics_ClearRuntimeExport();
    (void)Statistics_ClearPendingSession();
    free(pending->spans);
    memset(pending, 0, sizeof(*pending));
    g_statistics.pending_valid = FALSE;
    (void)Statistics_WriteSummaryExport();
    Statistics_RefreshOpenWindow();
    return TRUE;
}

void Statistics_OnFocusStepCompleted(void) {
    Statistics_Finalize(STATS_SESSION_COMPLETED);
}

void Statistics_OnFocusStepCancelled(void) {
    Statistics_Finalize(STATS_SESSION_CANCELLED);
}

BOOL Statistics_HasActiveSession(void) {
    return g_statistics.active_valid;
}

void Statistics_UpdateRuntimeExport(BOOL force) {
    if (g_statistics.storage_available) {
        (void)Statistics_WriteRuntimeExport(force);
    }
}

void Statistics_Shutdown(void) {
    Statistics_OnFocusStepCancelled();
    Statistics_FreeSessions();
    free(g_statistics.active.spans);
    free(g_statistics.pending.spans);
    memset(&g_statistics, 0, sizeof(g_statistics));
}
