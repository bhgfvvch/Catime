#include "statistics_internal.h"
#include "config.h"
#include "log.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "Assertion failed: %s (%s:%d)\n", \
            #condition, __FILE__, __LINE__); exit(1); } } while (0)

StatisticsState g_statistics = {0};
static BOOL s_flushSucceeds = TRUE;

void WriteLog(LogLevel level, const char* format, ...) {
    (void)level; (void)format;
}

BOOL GetEffectiveLocalAppDataPath(char* path, size_t size) {
    (void)path; (void)size; return FALSE;
}

BOOL Statistics_AddLoadedSession(const StatisticsSession* session) {
    StatisticsSession* resized = (StatisticsSession*)realloc(
        g_statistics.sessions,
        (g_statistics.session_count + 1) * sizeof(*resized));
    if (!resized) return FALSE;
    g_statistics.sessions = resized;
    StatisticsSession* copy = &resized[g_statistics.session_count++];
    *copy = *session;
    copy->spans = (StatisticsFocusSpan*)malloc(
        (size_t)session->span_count * sizeof(*copy->spans));
    if (!copy->spans) return FALSE;
    memcpy(copy->spans, session->spans,
           (size_t)session->span_count * sizeof(*copy->spans));
    return TRUE;
}

BOOL Statistics_FlushPending(void) {
    if (!g_statistics.pending_valid) return TRUE;
    if (!s_flushSucceeds) {
        return Statistics_SavePendingSession(&g_statistics.pending) && FALSE;
    }
    if (!Statistics_SessionIdOnDisk(g_statistics.pending.id) &&
        !Statistics_AppendSession(&g_statistics.pending)) return FALSE;
    if (!Statistics_AddLoadedSession(&g_statistics.pending)) return FALSE;
    (void)Statistics_ClearRuntimeExport();
    (void)Statistics_ClearPendingSession();
    free(g_statistics.pending.spans);
    memset(&g_statistics.pending, 0, sizeof(g_statistics.pending));
    g_statistics.pending_valid = FALSE;
    return TRUE;
}

static void ClearSessions(void) {
    for (size_t i = 0; i < g_statistics.session_count; ++i) {
        free(g_statistics.sessions[i].spans);
    }
    free(g_statistics.sessions);
    g_statistics.sessions = NULL;
    g_statistics.session_count = 0;
}

static void BuildPath(char* output, const char* file) {
    snprintf(output, MAX_PATH, "%s\\%s", g_statistics.directory, file);
}

int main(void) {
    char temporary[MAX_PATH];
    assert(GetTempPathA(MAX_PATH, temporary) > 0);
    snprintf(g_statistics.directory, sizeof(g_statistics.directory),
             "%sCatimeStatisticsTest-%lu", temporary, GetCurrentProcessId());
    g_statistics.storage_available = TRUE;
    assert(CreateDirectoryA(g_statistics.directory, NULL) ||
           GetLastError() == ERROR_ALREADY_EXISTS);
    char sessionsPath[MAX_PATH];
    BuildPath(sessionsPath, "sessions.jsonl");
    const char* valid =
        "{\"schema_version\":1,\"id\":1,\"start_utc_ms\":1000,"
        "\"end_utc_ms\":61000,\"planned_seconds\":60,"
        "\"focused_seconds\":60,\"paused_seconds\":0,"
        "\"status\":\"completed\",\"category_id\":\"study\","
        "\"category_name\":\"学习\",\"category_color\":\"#3A96DD\","
        "\"cycle\":1,\"step\":1,\"spans\":[[1000,61000]]}\n";
    char content[2048];
    snprintf(content, sizeof(content), "corrupt line\n%s", valid);
    assert(Statistics_AtomicReplace(sessionsPath, content));
    assert(Statistics_LoadSessions());
    assert(g_statistics.session_count == 1);
    assert(strcmp(g_statistics.sessions[0].category_name, "学习") == 0);

    char runtimePath[MAX_PATH];
    BuildPath(runtimePath, "runtime_state.json");
    const char* runtime =
        "{\"schema_version\":1,\"running\":true,\"session_id\":2,"
        "\"start_utc_ms\":100000,\"snapshot_utc_ms\":160000,"
        "\"planned_seconds\":1500,\"focused_seconds\":60,"
        "\"paused_seconds\":1200,\"cycle\":2,\"step\":3,"
        "\"category_id\":\"general\",\"category_name\":\"General\","
        "\"category_color\":\"#3A96DD\",\"spans\":[[100000,130000],"
        "[150000,160000]]}\n";
    assert(Statistics_AtomicReplace(runtimePath, runtime));
    s_flushSucceeds = FALSE;
    assert(!Statistics_RecoverRuntime());
    assert(g_statistics.pending_valid && g_statistics.pending.span_count == 2);
    s_flushSucceeds = TRUE;
    assert(Statistics_FlushPending());
    assert(g_statistics.session_count == 2);
    assert(g_statistics.sessions[1].status == STATS_SESSION_CANCELLED);
    assert(Statistics_RecoverRuntime());
    assert(g_statistics.session_count == 2);
    assert(g_statistics.sessions[1].span_count == 2);
    assert(g_statistics.sessions[1].paused_seconds == 1200);
    assert(g_statistics.sessions[1].pomodoro_cycle == 2);

    ClearSessions();
    const char* partial = "{\"schema_version\":1,\"id\":77,";
    assert(Statistics_AtomicReplace(sessionsPath, partial));
    assert(!Statistics_SessionIdOnDisk(77));
    const char* retry =
        "{\"schema_version\":1,\"id\":77,\"start_utc_ms\":1000,"
        "\"end_utc_ms\":61000,\"planned_seconds\":60,"
        "\"focused_seconds\":60,\"paused_seconds\":0,"
        "\"status\":\"cancelled\",\"category_id\":\"general\","
        "\"category_name\":\"General\",\"category_color\":\"#3A96DD\","
        "\"cycle\":1,\"step\":1,\"spans\":[[1000,61000]]}\n";
    StatisticsSession retried;
    assert(Statistics_ParseSessionLine(retry, &retried));
    assert(Statistics_AppendSession(&retried));
    free(retried.spans);
    assert(Statistics_SessionIdOnDisk(77));
    assert(Statistics_LoadSessions() && g_statistics.session_count == 1);

    ClearSessions();
    DeleteFileA(sessionsPath);
    DeleteFileA(runtimePath);
    char pendingPath[MAX_PATH];
    BuildPath(pendingPath, "pending_session.json");
    DeleteFileA(pendingPath);
    RemoveDirectoryA(g_statistics.directory);
    puts("statistics_storage_tests: PASS");
    return 0;
}
