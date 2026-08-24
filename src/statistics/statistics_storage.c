#include "statistics_internal.h"
#include "config.h"
#include "log.h"
#include <direct.h>
#include <io.h>
#include <limits.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>

static void BuildPath(char* output, size_t size, const char* name) {
    snprintf(output, size, "%s\\%s", g_statistics.directory, name);
}

BOOL Statistics_Utf8PathToWide(const char* path, wchar_t* output, size_t size) {
    return path && output && size > 0 && size <= INT_MAX &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            output, (int)size) > 0;
}

BOOL Statistics_EnsureDirectory(void) {
    char localAppData[MAX_PATH];
    if (!GetEffectiveLocalAppDataPath(localAppData, sizeof(localAppData))) {
        return FALSE;
    }
    if (snprintf(g_statistics.directory, sizeof(g_statistics.directory),
                 "%s\\Catime\\statistics", localAppData) < 0) return FALSE;
    wchar_t wide[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, g_statistics.directory, -1,
                            wide, MAX_PATH) <= 0) return FALSE;
    int result = SHCreateDirectoryExW(NULL, wide, NULL);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

BOOL Statistics_AtomicReplace(const char* path, const char* content) {
    char temporary[MAX_PATH];
    if (!path || !content || snprintf(temporary, sizeof(temporary),
        "%s.tmp", path) < 0) return FALSE;
    wchar_t temporaryWide[MAX_PATH];
    wchar_t pathWide[MAX_PATH];
    if (!Statistics_Utf8PathToWide(temporary, temporaryWide, MAX_PATH) ||
        !Statistics_Utf8PathToWide(path, pathWide, MAX_PATH)) return FALSE;
    FILE* file = NULL;
    if (_wfopen_s(&file, temporaryWide, L"wb") != 0 || !file) return FALSE;
    size_t length = strlen(content);
    BOOL ok = fwrite(content, 1, length, file) == length && fflush(file) == 0;
    if (ok) ok = FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(file)));
    fclose(file);
    if (!ok || !MoveFileExW(temporaryWide, pathWide,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryWide);
        return FALSE;
    }
    return TRUE;
}

BOOL Statistics_LoadSessions(void) {
    char path[MAX_PATH];
    BuildPath(path, sizeof(path), "sessions.jsonl");
    wchar_t pathWide[MAX_PATH];
    FILE* file = NULL;
    if (!Statistics_Utf8PathToWide(path, pathWide, MAX_PATH) ||
        _wfopen_s(&file, pathWide, L"rb") != 0 || !file) return TRUE;
    char line[16384];
    int lineNumber = 0;
    while (fgets(line, sizeof(line), file)) {
        lineNumber++;
        StatisticsSession session;
        if (!Statistics_ParseSessionLine(line, &session)) {
            LOG_WARNING("Ignoring corrupt statistics record at line %d", lineNumber);
            continue;
        }
        if (!Statistics_AddLoadedSession(&session)) {
            free(session.spans);
            fclose(file);
            return FALSE;
        }
        free(session.spans);
    }
    fclose(file);
    return TRUE;
}

BOOL Statistics_AppendSession(const StatisticsSession* session) {
    char path[MAX_PATH];
    BuildPath(path, sizeof(path), "sessions.jsonl");
    wchar_t pathWide[MAX_PATH];
    FILE* file = NULL;
    if (!session || !Statistics_Utf8PathToWide(path, pathWide, MAX_PATH) ||
        _wfopen_s(&file, pathWide, L"ab+") != 0 || !file) return FALSE;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return FALSE; }
    long size = ftell(file);
    if (size > 0) {
        if (fseek(file, -1, SEEK_END) != 0) { fclose(file); return FALSE; }
        int last = fgetc(file);
        if (fseek(file, 0, SEEK_END) != 0 ||
            (last != '\n' && fputc('\n', file) == EOF)) {
            fclose(file); return FALSE;
        }
    }
    fprintf(file, "{\"schema_version\":1,\"id\":%llu,\"start_utc_ms\":%lld,"
            "\"end_utc_ms\":%lld,\"planned_seconds\":%d,"
            "\"focused_seconds\":%d,\"paused_seconds\":%d,\"status\":",
            (unsigned long long)session->id, (long long)session->start_utc_ms,
            (long long)session->end_utc_ms, session->planned_seconds,
            session->focused_seconds, session->paused_seconds);
    Statistics_WriteJsonString(file, session->status == STATS_SESSION_COMPLETED
                                      ? "completed" : "cancelled");
    fputs(",\"category_id\":", file);
    Statistics_WriteJsonString(file, session->category_id);
    fputs(",\"category_name\":", file);
    Statistics_WriteJsonString(file, session->category_name);
    fputs(",\"category_color\":", file);
    Statistics_WriteJsonString(file, session->category_color);
    fprintf(file, ",\"cycle\":%d,\"step\":%d,\"spans\":[",
            session->pomodoro_cycle, session->pomodoro_step);
    for (int i = 0; i < session->span_count; ++i) {
        fprintf(file, "%s[%lld,%lld]", i ? "," : "",
                (long long)session->spans[i].start_utc_ms,
                (long long)session->spans[i].end_utc_ms);
    }
    fputs("]}\n", file);
    BOOL ok = fflush(file) == 0 &&
              FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(file)));
    fclose(file);
    return ok;
}

BOOL Statistics_ClearRuntimeExport(void) {
    char path[MAX_PATH];
    BuildPath(path, sizeof(path), "runtime_state.json");
    return Statistics_AtomicReplace(path,
        "{\"schema_version\":1,\"mode\":\"pomodoro\",\"running\":false}\n");
}

BOOL Statistics_RecoverRuntime(void) {
    char path[MAX_PATH];
    BuildPath(path, sizeof(path), "runtime_state.json");
    wchar_t pathWide[MAX_PATH];
    FILE* file = NULL;
    if (!Statistics_Utf8PathToWide(path, pathWide, MAX_PATH) ||
        _wfopen_s(&file, pathWide, L"rb") != 0 || !file) return TRUE;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return FALSE; }
    long length = ftell(file);
    if (length <= 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return FALSE;
    }
    char* json = (char*)calloc((size_t)length + 1, 1);
    if (!json) { fclose(file); return FALSE; }
    size_t read = fread(json, 1, (size_t)length, file);
    fclose(file);
    json[read] = '\0';
    if (!strstr(json, "\"running\":true")) { free(json); return TRUE; }
    StatisticsSession recovered;
    memset(&recovered, 0, sizeof(recovered));
    int64_t value;
    if (!Statistics_ReadJsonInt64(json, "session_id", &value)) {
        free(json); return FALSE;
    }
    recovered.id = (uint64_t)value;
    for (size_t i = 0; i < g_statistics.session_count; ++i) {
        if (g_statistics.sessions[i].id == recovered.id) {
            free(json); return Statistics_ClearRuntimeExport();
        }
    }
    if (!Statistics_ReadJsonInt64(json, "start_utc_ms", &recovered.start_utc_ms) ||
        !Statistics_ReadJsonInt64(json, "snapshot_utc_ms", &recovered.end_utc_ms) ||
        !Statistics_ReadJsonInt64(json, "focused_seconds", &value) || value <= 0) {
        free(json); return Statistics_ClearRuntimeExport();
    }
    recovered.focused_seconds = (int)value;
    Statistics_ReadJsonInt64(json, "planned_seconds", &value);
    recovered.planned_seconds = (int)value;
    if (Statistics_ReadJsonInt64(json, "paused_seconds", &value))
        recovered.paused_seconds = (int)value;
    if (Statistics_ReadJsonInt64(json, "cycle", &value))
        recovered.pomodoro_cycle = (int)value;
    if (Statistics_ReadJsonInt64(json, "step", &value))
        recovered.pomodoro_step = (int)value;
    recovered.status = STATS_SESSION_CANCELLED;
    Statistics_ReadJsonString(json, "category_id", recovered.category_id,
                              sizeof(recovered.category_id));
    Statistics_ReadJsonString(json, "category_name", recovered.category_name,
                              sizeof(recovered.category_name));
    Statistics_ReadJsonString(json, "category_color", recovered.category_color,
                              sizeof(recovered.category_color));
    if (!Statistics_ReadJsonSpans(json, &recovered)) {
        free(json);
        return Statistics_ClearRuntimeExport();
    }
    free(json);
    g_statistics.pending = recovered;
    g_statistics.pending_valid = TRUE;
    return Statistics_FlushPending();
}
