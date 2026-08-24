#include "statistics_internal.h"
#include <ctype.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

BOOL Statistics_WriteJsonString(FILE* file, const char* value) {
    if (!file || !value || fputc('"', file) == EOF) return FALSE;
    const unsigned char* p = (const unsigned char*)value;
    while (*p) {
        if (*p == '"' || *p == '\\') {
            if (fputc('\\', file) == EOF || fputc(*p, file) == EOF) return FALSE;
        } else if (*p == '\n' || *p == '\r' || *p == '\t') {
            const char escaped = *p == '\n' ? 'n' : (*p == '\r' ? 'r' : 't');
            if (fputc('\\', file) == EOF || fputc(escaped, file) == EOF) return FALSE;
        } else if (*p < 0x20) {
            if (fprintf(file, "\\u%04x", *p) < 0) return FALSE;
        } else if (fputc(*p, file) == EOF) {
            return FALSE;
        }
        ++p;
    }
    return fputc('"', file) != EOF;
}

static const char* FindValue(const char* json, const char* key) {
    char pattern[96];
    if (!json || !key || snprintf(pattern, sizeof(pattern), "\"%s\"", key) < 0) {
        return NULL;
    }
    const char* found = strstr(json, pattern);
    if (!found) return NULL;
    found += strlen(pattern);
    while (isspace((unsigned char)*found)) ++found;
    if (*found++ != ':') return NULL;
    while (isspace((unsigned char)*found)) ++found;
    return found;
}

BOOL Statistics_ReadJsonInt64(const char* json, const char* key,
                              int64_t* output) {
    if (!output) return FALSE;
    const char* value = FindValue(json, key);
    if (!value) return FALSE;
    char* end = NULL;
    __int64 parsed = _strtoi64(value, &end, 10);
    if (end == value) return FALSE;
    *output = (int64_t)parsed;
    return TRUE;
}

BOOL Statistics_ReadJsonString(const char* json, const char* key,
                               char* output, size_t output_size) {
    if (!output || output_size == 0) return FALSE;
    output[0] = '\0';
    const char* value = FindValue(json, key);
    if (!value || *value++ != '"') return FALSE;
    size_t used = 0;
    while (*value && *value != '"') {
        unsigned char ch = (unsigned char)*value++;
        if (ch == '\\') {
            ch = (unsigned char)*value++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch != '\\' && ch != '"' && ch != '/') return FALSE;
        }
        if (used + 1 >= output_size) return FALSE;
        output[used++] = (char)ch;
    }
    if (*value != '"') return FALSE;
    output[used] = '\0';
    return TRUE;
}

BOOL Statistics_ReadJsonSpans(const char* json, StatisticsSession* session) {
    if (!json || !session) return FALSE;
    const char* spans = FindValue(json, "spans");
    if (!spans || *spans++ != '[') return FALSE;
    while (*spans && *spans != ']') {
        while (isspace((unsigned char)*spans) || *spans == ',') ++spans;
        if (*spans++ != '[') goto parse_fail;
        char* end = NULL;
        int64_t start = _strtoi64(spans, &end, 10);
        if (end == spans || *end++ != ',') goto parse_fail;
        spans = end;
        int64_t finish = _strtoi64(spans, &end, 10);
        if (end == spans || *end++ != ']' || finish < start) goto parse_fail;
        if (session->span_count >= 4096) goto parse_fail;
        StatisticsFocusSpan* resized = (StatisticsFocusSpan*)realloc(
            session->spans, (size_t)(session->span_count + 1) * sizeof(*resized));
        if (!resized) goto parse_fail;
        session->spans = resized;
        session->span_capacity = session->span_count + 1;
        session->spans[session->span_count].start_utc_ms = start;
        session->spans[session->span_count++].end_utc_ms = finish;
        spans = end;
    }
    return session->span_count > 0;
parse_fail:
    free(session->spans);
    session->spans = NULL;
    session->span_count = 0;
    session->span_capacity = 0;
    return FALSE;
}

BOOL Statistics_ParseSessionLine(const char* line, StatisticsSession* session) {
    if (!line || !session) return FALSE;
    memset(session, 0, sizeof(*session));
    int64_t value;
    BOOL hasSchema = Statistics_ReadJsonInt64(line, "schema_version", &value);
    if (!hasSchema) hasSchema = Statistics_ReadJsonInt64(line, "schema", &value);
    if (!hasSchema || value != STATISTICS_SCHEMA_VERSION ||
        !Statistics_ReadJsonInt64(line, "id", &value)) return FALSE;
    session->id = (uint64_t)value;
    if (!Statistics_ReadJsonInt64(line, "start_utc_ms", &session->start_utc_ms) ||
        !Statistics_ReadJsonInt64(line, "end_utc_ms", &session->end_utc_ms) ||
        !Statistics_ReadJsonInt64(line, "planned_seconds", &value)) return FALSE;
    session->planned_seconds = (int)value;
    if (!Statistics_ReadJsonInt64(line, "focused_seconds", &value)) return FALSE;
    session->focused_seconds = (int)value;
    if (!Statistics_ReadJsonInt64(line, "paused_seconds", &value)) return FALSE;
    session->paused_seconds = (int)value;
    if (!Statistics_ReadJsonInt64(line, "cycle", &value)) return FALSE;
    session->pomodoro_cycle = (int)value;
    if (!Statistics_ReadJsonInt64(line, "step", &value)) return FALSE;
    session->pomodoro_step = (int)value;
    char status[16];
    if (!Statistics_ReadJsonString(line, "status", status, sizeof(status)) ||
        !Statistics_ReadJsonString(line, "category_id", session->category_id,
                                   sizeof(session->category_id)) ||
        !Statistics_ReadJsonString(line, "category_name", session->category_name,
                                   sizeof(session->category_name)) ||
        !Statistics_ReadJsonString(line, "category_color", session->category_color,
                                   sizeof(session->category_color))) return FALSE;
    if (strcmp(status, "completed") == 0) {
        session->status = STATS_SESSION_COMPLETED;
    } else if (strcmp(status, "cancelled") == 0) {
        session->status = STATS_SESSION_CANCELLED;
    } else {
        return FALSE;
    }
    if (!Statistics_ReadJsonSpans(line, session)) return FALSE;
    BOOL valid = session->id > 0 && session->focused_seconds > 0 &&
                 session->span_count > 0 &&
                 session->end_utc_ms >= session->start_utc_ms;
    if (!valid) {
        free(session->spans);
        session->spans = NULL;
    }
    return valid;
}
