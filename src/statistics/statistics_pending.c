#include "statistics_internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t used;
    size_t capacity;
} JsonBuffer;

static void BuildPath(char* output, size_t size) {
    snprintf(output, size, "%s\\pending_session.json", g_statistics.directory);
}

static BOOL Reserve(JsonBuffer* buffer, size_t extra) {
    if (buffer->used + extra + 1 <= buffer->capacity) return TRUE;
    size_t next = buffer->capacity ? buffer->capacity * 2 : 2048;
    while (next < buffer->used + extra + 1) next *= 2;
    char* resized = (char*)realloc(buffer->data, next);
    if (!resized) return FALSE;
    buffer->data = resized;
    buffer->capacity = next;
    return TRUE;
}

static BOOL AddText(JsonBuffer* buffer, const char* text) {
    size_t length = strlen(text);
    if (!Reserve(buffer, length)) return FALSE;
    memcpy(buffer->data + buffer->used, text, length + 1);
    buffer->used += length;
    return TRUE;
}

static BOOL AddFormat(JsonBuffer* buffer, const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return length >= 0 && (size_t)length < sizeof(text) && AddText(buffer, text);
}

static BOOL AddString(JsonBuffer* buffer, const char* value) {
    if (!AddText(buffer, "\"")) return FALSE;
    for (const unsigned char* p = (const unsigned char*)value; p && *p; ++p) {
        char escaped[8];
        if (*p == '"' || *p == '\\') {
            snprintf(escaped, sizeof(escaped), "\\%c", *p);
        } else if (*p < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
        } else {
            escaped[0] = (char)*p;
            escaped[1] = '\0';
        }
        if (!AddText(buffer, escaped)) return FALSE;
    }
    return AddText(buffer, "\"");
}

static char* Serialize(const StatisticsSession* session) {
    JsonBuffer out = {0};
    if (!AddFormat(&out, "{\"schema_version\":1,\"id\":%llu,"
        "\"start_utc_ms\":%lld,\"end_utc_ms\":%lld,"
        "\"planned_seconds\":%d,\"focused_seconds\":%d,"
        "\"paused_seconds\":%d,\"status\":",
        (unsigned long long)session->id, (long long)session->start_utc_ms,
        (long long)session->end_utc_ms, session->planned_seconds,
        session->focused_seconds, session->paused_seconds) ||
        !AddString(&out, session->status == STATS_SESSION_COMPLETED
            ? "completed" : "cancelled") ||
        !AddText(&out, ",\"category_id\":") ||
        !AddString(&out, session->category_id) ||
        !AddText(&out, ",\"category_name\":") ||
        !AddString(&out, session->category_name) ||
        !AddText(&out, ",\"category_color\":") ||
        !AddString(&out, session->category_color) ||
        !AddFormat(&out, ",\"cycle\":%d,\"step\":%d,\"spans\":[",
                   session->pomodoro_cycle, session->pomodoro_step)) goto fail;
    for (int i = 0; i < session->span_count; ++i) {
        if (!AddFormat(&out, "%s[%lld,%lld]", i ? "," : "",
                (long long)session->spans[i].start_utc_ms,
                (long long)session->spans[i].end_utc_ms)) goto fail;
    }
    if (!AddText(&out, "]}\n")) goto fail;
    return out.data;
fail:
    free(out.data);
    return NULL;
}

static char* Statistics_ReadTextFile(const char* path) {
    wchar_t wide[MAX_PATH];
    FILE* file = NULL;
    if (!Statistics_Utf8PathToWide(path, wide, MAX_PATH) ||
        _wfopen_s(&file, wide, L"rb") != 0 || !file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size <= 0 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    char* content = (char*)malloc((size_t)size + 1);
    if (!content) { fclose(file); return NULL; }
    size_t read = fread(content, 1, (size_t)size, file);
    fclose(file);
    content[read] = '\0';
    return content;
}

BOOL Statistics_SavePendingSession(const StatisticsSession* session) {
    if (!session || !g_statistics.storage_available) return FALSE;
    char* json = Serialize(session);
    if (!json) return FALSE;
    char path[MAX_PATH];
    BuildPath(path, sizeof(path));
    BOOL result = Statistics_AtomicReplace(path, json);
    free(json);
    return result;
}

BOOL Statistics_LoadPendingSession(StatisticsSession* session) {
    char path[MAX_PATH];
    BuildPath(path, sizeof(path));
    char* json = Statistics_ReadTextFile(path);
    if (!json) return FALSE;
    BOOL result = Statistics_ParseSessionLine(json, session);
    free(json);
    return result;
}

BOOL Statistics_ClearPendingSession(void) {
    char path[MAX_PATH];
    wchar_t wide[MAX_PATH];
    BuildPath(path, sizeof(path));
    if (!Statistics_Utf8PathToWide(path, wide, MAX_PATH)) return FALSE;
    return DeleteFileW(wide) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

BOOL Statistics_SessionIdOnDisk(uint64_t id) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\sessions.jsonl", g_statistics.directory);
    wchar_t wide[MAX_PATH];
    FILE* file = NULL;
    if (!Statistics_Utf8PathToWide(path, wide, MAX_PATH) ||
        _wfopen_s(&file, wide, L"rb") != 0 || !file) return FALSE;
    char line[16384];
    BOOL found = FALSE;
    while (!found && fgets(line, sizeof(line), file)) {
        StatisticsSession parsed;
        if (Statistics_ParseSessionLine(line, &parsed)) {
            found = parsed.id == id;
            free(parsed.spans);
        }
    }
    fclose(file);
    return found;
}
