#ifndef CATIME_STATISTICS_INTERNAL_H
#define CATIME_STATISTICS_INTERNAL_H

#include "statistics/statistics.h"
#include <stdio.h>

typedef struct {
    BOOL initialized;
    BOOL storage_available;
    StatisticsSession* sessions;
    size_t session_count;
    size_t session_capacity;
    StatisticsCategory categories[STATISTICS_MAX_CATEGORIES];
    int category_count;
    int selected_category;
    StatisticsSession active;
    BOOL active_valid;
    BOOL active_paused;
    StatisticsSession pending;
    BOOL pending_valid;
    int64_t active_start_mono_ms;
    int64_t pause_start_mono_ms;
    int64_t focused_mono_ms;
    int64_t paused_mono_ms;
    int64_t last_runtime_write_mono_ms;
    uint64_t next_id;
    char directory[MAX_PATH];
} StatisticsState;

extern StatisticsState g_statistics;

int64_t Statistics_UtcNowMs(void);
int64_t Statistics_MonotonicNowMs(void);
BOOL Statistics_EnsureDirectory(void);
BOOL Statistics_LoadSessions(void);
BOOL Statistics_AppendSession(const StatisticsSession* session);
BOOL Statistics_SavePendingSession(const StatisticsSession* session);
BOOL Statistics_LoadPendingSession(StatisticsSession* session);
BOOL Statistics_ClearPendingSession(void);
BOOL Statistics_SessionIdOnDisk(uint64_t id);
BOOL Statistics_FlushPending(void);
BOOL Statistics_LoadCategories(void);
BOOL Statistics_SaveCategories(void);
BOOL Statistics_WriteSummaryExport(void);
BOOL Statistics_WriteRuntimeExport(BOOL force);
BOOL Statistics_ClearRuntimeExport(void);
BOOL Statistics_RecoverRuntime(void);
BOOL Statistics_AddLoadedSession(const StatisticsSession* session);
void Statistics_FreeSessions(void);
BOOL Statistics_ParseSessionLine(const char* line, StatisticsSession* session);
BOOL Statistics_WriteJsonString(FILE* file, const char* value);
BOOL Statistics_ReadJsonString(const char* json, const char* key,
                               char* output, size_t output_size);
BOOL Statistics_ReadJsonInt64(const char* json, const char* key,
                              int64_t* output);
BOOL Statistics_ReadJsonSpans(const char* json, StatisticsSession* session);
BOOL Statistics_AtomicReplace(const char* path, const char* content);
BOOL Statistics_Utf8PathToWide(const char* path, wchar_t* output, size_t size);
void Statistics_CloseActiveSpan(int64_t utc_ms);
void Statistics_Finalize(StatisticsSessionStatus status);
BOOL Statistics_DateRange(StatisticsRangeKind range, const SYSTEMTIME* anchor,
                          int64_t* start_utc_ms, int64_t* end_utc_ms);
BOOL Statistics_UtcToLocalDate(int64_t utc_ms, SYSTEMTIME* local);
int64_t Statistics_LocalDateToUtcMs(const SYSTEMTIME* local);
void Statistics_AddDays(SYSTEMTIME* date, int days);
int Statistics_CompareDate(const SYSTEMTIME* a, const SYSTEMTIME* b);

#endif
