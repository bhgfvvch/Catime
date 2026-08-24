#ifndef CATIME_STATISTICS_H
#define CATIME_STATISTICS_H

#include "statistics/statistics_types.h"

typedef int64_t (*StatisticsClockFn)(void);

BOOL Statistics_Initialize(void);
void Statistics_Shutdown(void);
void Statistics_SetClocksForTesting(StatisticsClockFn utcClock,
                                    StatisticsClockFn monotonicClock);

PomodoroStepKind Pomodoro_GetStepKind(int index);
void Statistics_OnFocusStepStarted(int plannedSeconds, int cycle, int step);
void Statistics_OnPause(void);
void Statistics_OnResume(void);
void Statistics_OnFocusStepCompleted(void);
void Statistics_OnFocusStepCancelled(void);
void Statistics_UpdateRuntimeExport(BOOL force);
BOOL Statistics_HasActiveSession(void);

BOOL Statistics_Query(StatisticsRangeKind range, const SYSTEMTIME* anchor,
                      StatisticsSummary* summary);

int Statistics_GetCategories(StatisticsCategory* output, int capacity);
BOOL Statistics_GetSelectedCategory(StatisticsCategory* output);
BOOL Statistics_SelectCategory(const char* id);
BOOL Statistics_AddCategory(const char* name, const char* color);
BOOL Statistics_RenameCategory(const char* id, const char* name);
BOOL Statistics_DeleteCategory(const char* id);
BOOL Statistics_SetCategoryColor(const char* id, const char* color);
BOOL Statistics_MoveCategory(const char* id, int direction);

void Statistics_ShowWindow(HWND owner);
void Statistics_ShowCategoryManager(HWND owner);
void Statistics_RefreshOpenWindow(void);

#endif
