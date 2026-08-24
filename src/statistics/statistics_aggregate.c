#include "statistics_internal.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    SYSTEMTIME date;
    int64_t focused_ms;
    int64_t category_ms[STATISTICS_MAX_CATEGORIES];
} DayBucket;
typedef struct {
    char id[STATISTICS_CATEGORY_ID_MAX];
    char name[STATISTICS_CATEGORY_NAME_MAX];
    char color[STATISTICS_COLOR_MAX];
    int64_t focused_ms;
} CategoryContribution;
static int CompareContributionId(const void* left, const void* right) {
    return strcmp(((const CategoryContribution*)left)->id,
                  ((const CategoryContribution*)right)->id);
}
static int CompareContributionTime(const void* left, const void* right) {
    const CategoryContribution* a = (const CategoryContribution*)left;
    const CategoryContribution* b = (const CategoryContribution*)right;
    return a->focused_ms < b->focused_ms ? 1 :
           (a->focused_ms > b->focused_ms ? -1 : strcmp(a->id, b->id));
}
static int CompareBuckets(const void* left, const void* right) {
    const DayBucket* a = (const DayBucket*)left;
    const DayBucket* b = (const DayBucket*)right;
    return Statistics_CompareDate(&a->date, &b->date);
}
static DayBucket* FindOrAddBucket(DayBucket** buckets, size_t* count,
                                  size_t* capacity, const SYSTEMTIME* date) {
    size_t low = 0, high = *count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int comparison = Statistics_CompareDate(&(*buckets)[middle].date, date);
        if (comparison < 0) low = middle + 1;
        else high = middle;
    }
    if (low < *count &&
        Statistics_CompareDate(&(*buckets)[low].date, date) == 0) {
        return &(*buckets)[low];
    }
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 64;
        DayBucket* resized = (DayBucket*)realloc(*buckets, next * sizeof(**buckets));
        if (!resized) return NULL;
        *buckets = resized;
        *capacity = next;
    }
    if (low < *count) {
        memmove(&(*buckets)[low + 1], &(*buckets)[low],
                (*count - low) * sizeof(**buckets));
    }
    (*count)++;
    DayBucket* bucket = &(*buckets)[low];
    memset(bucket, 0, sizeof(*bucket));
    bucket->date = *date;
    bucket->date.wHour = bucket->date.wMinute = bucket->date.wSecond = 0;
    bucket->date.wMilliseconds = 0;
    return bucket;
}
static void AddSpanToDays(int64_t spanStart, int64_t spanEnd,
                          int category, DayBucket** buckets, size_t* count,
                          size_t* capacity) {
    if (spanEnd <= spanStart) return;
    SYSTEMTIME date;
    if (!Statistics_UtcToLocalDate(spanStart, &date)) return;
    date.wHour = date.wMinute = date.wSecond = date.wMilliseconds = 0;
    while (spanStart < spanEnd) {
        SYSTEMTIME next = date;
        Statistics_AddDays(&next, 1);
        int64_t boundary = Statistics_LocalDateToUtcMs(&next);
        if (boundary <= spanStart) break;
        int64_t partEnd = boundary < spanEnd ? boundary : spanEnd;
        DayBucket* bucket = FindOrAddBucket(buckets, count, capacity, &date);
        if (!bucket) return;
        bucket->focused_ms += partEnd - spanStart;
        if (category >= 0 && category < STATISTICS_MAX_CATEGORIES) {
            bucket->category_ms[category] += partEnd - spanStart;
        }
        spanStart = partEnd;
        date = next;
    }
}
static int OutputCategory(const StatisticsSummary* summary, const char* id) {
    for (int i = 0; i < summary->category_count; ++i) {
        if (strcmp(summary->categories[i].id, id) == 0) return i;
    }
    return summary->category_count == STATISTICS_MAX_CATEGORIES
        ? STATISTICS_MAX_CATEGORIES - 1 : -1;
}
static int64_t SessionOverlapMs(const StatisticsSession* session,
                                int64_t start, int64_t end, int category,
                                DayBucket** allDays, size_t* allCount,
                                size_t* allCapacity) {
    int64_t overlap = 0; for (int i = 0; i < session->span_count; ++i) {
        int64_t spanStart = session->spans[i].start_utc_ms;
        int64_t spanEnd = session->spans[i].end_utc_ms;
        if (spanEnd == 0) spanEnd = Statistics_UtcNowMs();
        AddSpanToDays(spanStart, spanEnd, category,
                      allDays, allCount, allCapacity);
        int64_t clippedStart = spanStart > start ? spanStart : start;
        int64_t clippedEnd = spanEnd < end ? spanEnd : end;
        if (clippedEnd > clippedStart) overlap += clippedEnd - clippedStart;
    }
    return overlap;
}
static void CalculateStreaks(const DayBucket* days, size_t count,
                             StatisticsSummary* summary) {
    int run = 0; SYSTEMTIME previous = {0};
    for (size_t i = 0; i < count; ++i) {
        if (days[i].focused_ms <= 0) continue;
        SYSTEMTIME expected = previous;
        if (run > 0) Statistics_AddDays(&expected, 1);
        run = run > 0 && Statistics_CompareDate(&expected, &days[i].date) == 0
            ? run + 1 : 1;
        if (run > summary->longest_streak) summary->longest_streak = run;
        previous = days[i].date;
    }
    if (run == 0) return;
    SYSTEMTIME nowUtc, today;
    GetSystemTime(&nowUtc);
    SystemTimeToTzSpecificLocalTime(NULL, &nowUtc, &today);
    SYSTEMTIME yesterday = today;
    Statistics_AddDays(&yesterday, -1);
    if (Statistics_CompareDate(&previous, &today) == 0 ||
        Statistics_CompareDate(&previous, &yesterday) == 0) {
        summary->current_streak = run;
    }
}
static int64_t ClippedOverlapMs(const StatisticsSession* session, int64_t start, int64_t end) {
    int64_t total = 0;
    for (int i = 0; i < session->span_count; ++i) {
        int64_t spanEnd = session->spans[i].end_utc_ms;
        if (spanEnd == 0) spanEnd = Statistics_UtcNowMs();
        int64_t clippedStart = session->spans[i].start_utc_ms > start
            ? session->spans[i].start_utc_ms : start;
        int64_t clippedEnd = spanEnd < end ? spanEnd : end;
        if (clippedEnd > clippedStart) total += clippedEnd - clippedStart;
    }
    return total;
}
static void CopyContribution(CategoryContribution* value, const StatisticsSession* session, int64_t ms) {
    memset(value, 0, sizeof(*value));
    strncpy_s(value->id, sizeof(value->id), session->category_id, _TRUNCATE);
    strncpy_s(value->name, sizeof(value->name), session->category_name, _TRUNCATE);
    strncpy_s(value->color, sizeof(value->color), session->category_color, _TRUNCATE);
    value->focused_ms = ms;
}
static size_t MergeContributions(CategoryContribution* values, size_t count) {
    if (count == 0) return 0;
    qsort(values, count, sizeof(*values), CompareContributionId);
    size_t merged = 0;
    for (size_t i = 0; i < count; ++i) {
        if (merged > 0 && strcmp(values[merged - 1].id, values[i].id) == 0) {
            values[merged - 1].focused_ms += values[i].focused_ms;
        } else {
            values[merged++] = values[i];
        }
    }
    qsort(values, merged, sizeof(*values), CompareContributionTime);
    return merged;
}
static void SelectCategories(StatisticsSummary* summary, CategoryContribution* values, size_t count) {
    int top = count > STATISTICS_MAX_CATEGORIES ? STATISTICS_MAX_CATEGORIES - 1 : (int)count;
    int64_t totalMs = 0; for (size_t i = 0; i < count; ++i) totalMs += values[i].focused_ms;
    for (int i = 0; i < top; ++i) {
        strncpy_s(summary->categories[i].id, sizeof(summary->categories[i].id),
                  values[i].id, _TRUNCATE);
        strncpy_s(summary->categories[i].name, sizeof(summary->categories[i].name),
                  values[i].name, _TRUNCATE);
        strncpy_s(summary->categories[i].color, sizeof(summary->categories[i].color),
                  values[i].color, _TRUNCATE);
        summary->categories[i].focused_seconds = values[i].focused_ms / 1000;
    }
    summary->category_count = top;
    if (count > STATISTICS_MAX_CATEGORIES) {
        StatisticsCategoryValue* other = &summary->categories[top];
        strcpy_s(other->id, sizeof(other->id), "other");
        strcpy_s(other->name, sizeof(other->name), "Other");
        strcpy_s(other->color, sizeof(other->color), "#808080");
        int64_t otherMs = 0;
        for (size_t i = (size_t)top; i < count; ++i) otherMs += values[i].focused_ms;
        other->focused_seconds = otherMs / 1000;
        summary->category_count++;
    }
    int64_t assigned = 0; for (int i = 0; i < summary->category_count; ++i) assigned += summary->categories[i].focused_seconds;
    if (summary->category_count > 0) {
        summary->categories[summary->category_count - 1].focused_seconds += totalMs / 1000 - assigned;
    }
}
static void PopulateDayValues(StatisticsSummary* summary, const DayBucket* days,
                              size_t count, int64_t start, int64_t end,
                              StatisticsRangeKind range) {
    for (size_t i = 0; i < count; ++i) {
        int64_t dayStart = Statistics_LocalDateToUtcMs(&days[i].date);
        if (dayStart < start || dayStart >= end) continue;
        int64_t seconds = days[i].focused_ms / 1000;
        if (seconds <= 0) continue;
        summary->active_days++;
        if (seconds > summary->best_day_seconds) {
            summary->best_day_seconds = seconds;
            summary->best_day = days[i].date;
        }
    }
    if (range != STATS_RANGE_ALL) {
        SYSTEMTIME date;
        if (Statistics_UtcToLocalDate(start, &date)) {
            date.wHour = date.wMinute = date.wSecond = date.wMilliseconds = 0;
            while (Statistics_LocalDateToUtcMs(&date) < end &&
                   summary->day_count < STATISTICS_MAX_DAYS) {
                int index = summary->day_count++;
                summary->days[index].date = date;
                for (size_t i = 0; i < count; ++i) {
                    if (Statistics_CompareDate(&days[i].date, &date) != 0) continue;
                    summary->days[index].focused_seconds = days[i].focused_ms / 1000;
                    for (int category = 0;
                         category < summary->category_count; ++category) {
                        summary->day_category_seconds[index][category] =
                            days[i].category_ms[category] / 1000;
                    }
                    break;
                }
                Statistics_AddDays(&date, 1);
            }
        }
    }
    if (summary->active_days > 0) {
        summary->average_active_day_seconds =
            summary->total_focus_seconds / summary->active_days;
    }
}
static void CalculatePercentages(StatisticsSummary* summary) {
    if (summary->total_focus_seconds <= 0) return;
    int assigned = 0; int64_t remainders[STATISTICS_MAX_CATEGORIES] = {0};
    for (int i = 0; i < summary->category_count; ++i) {
        int64_t scaled = summary->categories[i].focused_seconds * 100;
        summary->categories[i].percentage = (int)(scaled / summary->total_focus_seconds);
        remainders[i] = scaled % summary->total_focus_seconds;
        assigned += summary->categories[i].percentage;
    }
    for (int point = assigned; point < 100; ++point) {
        int best = 0;
        for (int i = 1; i < summary->category_count; ++i) {
            if (remainders[i] > remainders[best]) best = i;
        }
        summary->categories[best].percentage++;
        remainders[best] = -1;
    }
}
BOOL Statistics_Query(StatisticsRangeKind range, const SYSTEMTIME* anchor, StatisticsSummary* summary) {
    if (!summary) return FALSE; memset(summary, 0, sizeof(*summary));
    int64_t start, end;
    if (!Statistics_DateRange(range, anchor, &start, &end)) return FALSE;
    DayBucket* days = NULL;
    size_t dayCount = 0, dayCapacity = 0;
    size_t contributionCapacity = g_statistics.session_count + (g_statistics.active_valid ? 1u : 0u);
    CategoryContribution* contributions = contributionCapacity ? (CategoryContribution*)calloc(contributionCapacity, sizeof(*contributions)) : NULL;
    size_t contributionCount = 0; int64_t totalMs = 0;
    for (size_t i = 0; i < g_statistics.session_count; ++i) {
        const StatisticsSession* session = &g_statistics.sessions[i];
        int64_t overlap = ClippedOverlapMs(session, start, end);
        if (overlap <= 0) continue;
        totalMs += overlap;
        if (contributions) CopyContribution(&contributions[contributionCount++], session, overlap);
        if (session->end_utc_ms >= start && session->end_utc_ms < end) {
            if (session->status == STATS_SESSION_COMPLETED) summary->completed_sessions++;
            else summary->cancelled_sessions++;
        }
    }
    if (g_statistics.active_valid) {
        int64_t overlap = ClippedOverlapMs(&g_statistics.active, start, end);
        if (overlap > 0) {
            totalMs += overlap;
            if (contributions) CopyContribution(&contributions[contributionCount++],
                                                &g_statistics.active, overlap);
        }
    }
    size_t uniqueCount = MergeContributions(contributions, contributionCount);
    SelectCategories(summary, contributions, uniqueCount);
    for (size_t i = 0; i < g_statistics.session_count; ++i) {
        const StatisticsSession* session = &g_statistics.sessions[i];
        int category = OutputCategory(summary, session->category_id);
        (void)SessionOverlapMs(session, start, end, category,
                               &days, &dayCount, &dayCapacity);
    }
    if (g_statistics.active_valid) {
        int category = OutputCategory(summary, g_statistics.active.category_id);
        (void)SessionOverlapMs(&g_statistics.active, start, end, category,
                               &days, &dayCount, &dayCapacity);
    }
    summary->total_focus_seconds = totalMs / 1000;
    qsort(days, dayCount, sizeof(*days), CompareBuckets);
    CalculateStreaks(days, dayCount, summary);
    PopulateDayValues(summary, days, dayCount, start, end, range);
    CalculatePercentages(summary);
    free(contributions);
    free(days);
    return TRUE;
}
