#include "statistics_internal.h"
#include "log.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t s_utc = 1000000000, s_mono = 5000;
static BOOL s_append_ok = TRUE;
static int s_clear_count;
static int64_t ClockUtc(void) { return s_utc; }
static int64_t ClockMono(void) { return s_mono; }
static void Advance(int seconds) { s_utc += seconds * 1000LL; s_mono += seconds * 1000LL; }
void WriteLog(LogLevel level, const char* format, ...) { (void)level; (void)format; }
BOOL Statistics_EnsureDirectory(void) { return FALSE; }
BOOL Statistics_LoadSessions(void) { return TRUE; }
BOOL Statistics_LoadCategories(void) { return TRUE; }
BOOL Statistics_SaveCategories(void) { return TRUE; }
BOOL Statistics_RecoverRuntime(void) { return TRUE; }
BOOL Statistics_WriteSummaryExport(void) { return TRUE; }
BOOL Statistics_WriteRuntimeExport(BOOL force) { (void)force; return TRUE; }
BOOL Statistics_ClearRuntimeExport(void) { ++s_clear_count; return TRUE; }
BOOL Statistics_SavePendingSession(const StatisticsSession* session) { (void)session; return TRUE; }
BOOL Statistics_LoadPendingSession(StatisticsSession* session) { (void)session; return FALSE; }
BOOL Statistics_ClearPendingSession(void) { return TRUE; }
BOOL Statistics_SessionIdOnDisk(uint64_t id) { (void)id; return FALSE; }
BOOL Statistics_AppendSession(const StatisticsSession* session) { (void)session; return s_append_ok; }
void Statistics_RefreshOpenWindow(void) {}
BOOL Statistics_GetSelectedCategory(StatisticsCategory* output) {
    if (!output) return FALSE; *output = g_statistics.categories[0]; return TRUE;
}
static void Reset(void) {
    Statistics_FreeSessions(); free(g_statistics.active.spans); free(g_statistics.pending.spans);
    memset(&g_statistics, 0, sizeof(g_statistics));
    g_statistics.storage_available = TRUE; g_statistics.initialized = TRUE; g_statistics.next_id = 1;
    strcpy_s(g_statistics.categories[0].id, sizeof(g_statistics.categories[0].id), "general");
    strcpy_s(g_statistics.categories[0].name, sizeof(g_statistics.categories[0].name), "General");
    strcpy_s(g_statistics.categories[0].color, sizeof(g_statistics.categories[0].color), "#3A96DD");
    g_statistics.categories[0].enabled = TRUE; g_statistics.category_count = 1;
    s_utc = 1000000000; s_mono = 5000; s_append_ok = TRUE; s_clear_count = 0;
    Statistics_SetClocksForTesting(ClockUtc, ClockMono);
}
int main(void) {
    Reset(); g_statistics.initialized = FALSE; g_statistics.storage_available = FALSE;
    assert(!Statistics_Initialize()); assert(!g_statistics.initialized);
    Reset(); Statistics_OnFocusStepStarted(300, 1, 1); Advance(2); s_append_ok = FALSE;
    Statistics_OnFocusStepCancelled(); assert(g_statistics.pending_valid); assert(s_clear_count == 0);
    Statistics_OnFocusStepStarted(300, 1, 1);
    assert(g_statistics.pending_valid && !g_statistics.active_valid);
    s_append_ok = TRUE; assert(Statistics_FlushPending()); assert(!g_statistics.pending_valid);
    assert(g_statistics.session_count == 1); Statistics_OnFocusStepCancelled(); assert(g_statistics.session_count == 1);
    Reset(); Statistics_OnFocusStepStarted(300, 1, 1); Advance(1); Statistics_OnPause();
    Statistics_OnResume(); Advance(1); Statistics_OnFocusStepCompleted(); assert(g_statistics.session_count == 1);
    Reset(); Statistics_OnFocusStepStarted(300, 1, 1); Advance(1);
    Statistics_OnFocusStepStarted(300, 1, 1); assert(g_statistics.session_count == 1);
    Advance(1); Statistics_OnFocusStepCancelled(); Statistics_OnFocusStepCancelled();
    assert(g_statistics.session_count == 2);
    assert(Pomodoro_GetStepKind(1) == POMODORO_STEP_BREAK);
    Reset(); Statistics_OnFocusStepStarted(300, 1, 1); Advance(1); Statistics_OnFocusStepCompleted();
    Statistics_OnFocusStepStarted(300, 2, 3); Advance(1); Statistics_OnFocusStepCompleted();
    assert(g_statistics.session_count == 2);
    puts("statistics_reliability_tests: PASS"); return 0;
}
