#include "statistics_internal.h"
#include <time.h>

#define WINDOWS_EPOCH_100NS 116444736000000000LL

static int64_t FileTimeToUnixMs(const FILETIME* fileTime) {
    ULARGE_INTEGER value;
    value.LowPart = fileTime->dwLowDateTime;
    value.HighPart = fileTime->dwHighDateTime;
    return ((int64_t)value.QuadPart - WINDOWS_EPOCH_100NS) / 10000;
}

static void UnixMsToFileTime(int64_t value, FILETIME* fileTime) {
    ULARGE_INTEGER encoded;
    encoded.QuadPart = (uint64_t)(value * 10000 + WINDOWS_EPOCH_100NS);
    fileTime->dwLowDateTime = encoded.LowPart;
    fileTime->dwHighDateTime = encoded.HighPart;
}

BOOL Statistics_UtcToLocalDate(int64_t utc_ms, SYSTEMTIME* local) {
    if (!local) return FALSE;
    FILETIME fileTime;
    SYSTEMTIME utc;
    UnixMsToFileTime(utc_ms, &fileTime);
    if (!FileTimeToSystemTime(&fileTime, &utc)) return FALSE;
    return SystemTimeToTzSpecificLocalTime(NULL, &utc, local);
}

int64_t Statistics_LocalDateToUtcMs(const SYSTEMTIME* local) {
    if (!local) return -1;
    SYSTEMTIME midnight = *local;
    SYSTEMTIME utc;
    FILETIME fileTime;
    midnight.wHour = 0;
    midnight.wMinute = 0;
    midnight.wSecond = 0;
    midnight.wMilliseconds = 0;
    if (!TzSpecificLocalTimeToSystemTime(NULL, &midnight, &utc) ||
        !SystemTimeToFileTime(&utc, &fileTime)) {
        return -1;
    }
    return FileTimeToUnixMs(&fileTime);
}

void Statistics_AddDays(SYSTEMTIME* date, int days) {
    if (!date || days == 0) return;
    SYSTEMTIME noon = *date;
    FILETIME fileTime;
    ULARGE_INTEGER value;
    noon.wHour = 12;
    noon.wMinute = noon.wSecond = noon.wMilliseconds = 0;
    if (!SystemTimeToFileTime(&noon, &fileTime)) return;
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    value.QuadPart += (int64_t)days * 864000000000LL;
    fileTime.dwLowDateTime = value.LowPart;
    fileTime.dwHighDateTime = value.HighPart;
    FileTimeToSystemTime(&fileTime, date);
    date->wHour = date->wMinute = date->wSecond = date->wMilliseconds = 0;
}

int Statistics_CompareDate(const SYSTEMTIME* a, const SYSTEMTIME* b) {
    if (a->wYear != b->wYear) return a->wYear < b->wYear ? -1 : 1;
    if (a->wMonth != b->wMonth) return a->wMonth < b->wMonth ? -1 : 1;
    if (a->wDay != b->wDay) return a->wDay < b->wDay ? -1 : 1;
    return 0;
}

static void NormalizeAnchor(const SYSTEMTIME* anchor, SYSTEMTIME* result) {
    if (anchor) {
        *result = *anchor;
    } else {
        SYSTEMTIME utc;
        GetSystemTime(&utc);
        SystemTimeToTzSpecificLocalTime(NULL, &utc, result);
    }
    result->wHour = result->wMinute = result->wSecond = 0;
    result->wMilliseconds = 0;
}

BOOL Statistics_DateRange(StatisticsRangeKind range, const SYSTEMTIME* anchor,
                          int64_t* start_utc_ms, int64_t* end_utc_ms) {
    if (!start_utc_ms || !end_utc_ms) return FALSE;
    if (range == STATS_RANGE_ALL) {
        *start_utc_ms = 0;
        *end_utc_ms = INT64_MAX;
        return TRUE;
    }

    SYSTEMTIME start;
    SYSTEMTIME end;
    NormalizeAnchor(anchor, &start);
    if (range == STATS_RANGE_SEVEN_DAYS) {
        int offset = start.wDayOfWeek == 0 ? 6 : start.wDayOfWeek - 1;
        Statistics_AddDays(&start, -offset);
    } else if (range == STATS_RANGE_MONTH) {
        start.wDay = 1;
    } else if (range == STATS_RANGE_YEAR) {
        start.wMonth = 1;
        start.wDay = 1;
    }
    end = start;
    if (range == STATS_RANGE_TODAY) Statistics_AddDays(&end, 1);
    if (range == STATS_RANGE_SEVEN_DAYS) Statistics_AddDays(&end, 7);
    if (range == STATS_RANGE_MONTH) {
        end.wMonth++;
        if (end.wMonth > 12) { end.wMonth = 1; end.wYear++; }
    }
    if (range == STATS_RANGE_YEAR) end.wYear++;
    *start_utc_ms = Statistics_LocalDateToUtcMs(&start);
    *end_utc_ms = Statistics_LocalDateToUtcMs(&end);
    return *start_utc_ms >= 0 && *end_utc_ms > *start_utc_ms;
}
