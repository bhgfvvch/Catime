#include "statistics_internal.h"
#include "log.h"
#include "color/color_parser.h"
#include <ctype.h>
#include <string.h>

static const char* CATEGORY_COLORS[] = {
    "#3A96DD", "#57A773", "#E0A458", "#D65D7A",
    "#7B61A8", "#2A9D8F", "#D97706", "#64748B"
};

typedef struct {
    StatisticsCategory categories[STATISTICS_MAX_CATEGORIES];
    int category_count;
    int selected_category;
    uint64_t next_id;
} CategorySnapshot;

static void TakeSnapshot(CategorySnapshot* snapshot) {
    memcpy(snapshot->categories, g_statistics.categories,
           sizeof(snapshot->categories));
    snapshot->category_count = g_statistics.category_count;
    snapshot->selected_category = g_statistics.selected_category;
    snapshot->next_id = g_statistics.next_id;
}

static BOOL CommitOrRestore(const CategorySnapshot* snapshot) {
    if (Statistics_SaveCategories()) return TRUE;
    memcpy(g_statistics.categories, snapshot->categories,
           sizeof(snapshot->categories));
    g_statistics.category_count = snapshot->category_count;
    g_statistics.selected_category = snapshot->selected_category;
    g_statistics.next_id = snapshot->next_id;
    return FALSE;
}

static void SetGeneral(void) {
    memset(g_statistics.categories, 0, sizeof(g_statistics.categories));
    StatisticsCategory* general = &g_statistics.categories[0];
    strcpy_s(general->id, sizeof(general->id), "general");
    strcpy_s(general->name, sizeof(general->name), "General");
    strcpy_s(general->color, sizeof(general->color), "#3A96DD");
    general->enabled = TRUE;
    general->order = 0;
    g_statistics.category_count = 1;
    g_statistics.selected_category = 0;
}

static int FindCategory(const char* id) {
    if (!id) return -1;
    for (int i = 0; i < g_statistics.category_count; ++i) {
        if (strcmp(g_statistics.categories[i].id, id) == 0) return i;
    }
    return -1;
}

static BOOL ValidUtf8(const char* value) {
    return value && value[0] && MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0) > 0;
}

static BOOL NameAvailable(const char* name, int except) {
    if (!ValidUtf8(name) || strlen(name) >= STATISTICS_CATEGORY_NAME_MAX) return FALSE;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
        if (*p < 0x20) return FALSE;
    }
    for (int i = 0; i < g_statistics.category_count; ++i) {
        if (i != except && _stricmp(g_statistics.categories[i].name, name) == 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL AppendText(char* buffer, size_t size, size_t* used,
                       const char* text) {
    size_t length = strlen(text);
    if (*used + length >= size) return FALSE;
    memcpy(buffer + *used, text, length);
    *used += length;
    buffer[*used] = '\0';
    return TRUE;
}

static BOOL AppendString(char* buffer, size_t size, size_t* used,
                         const char* value) {
    if (!AppendText(buffer, size, used, "\"")) return FALSE;
    for (const unsigned char* p = (const unsigned char*)value; *p; ++p) {
        char encoded[3] = {(char)*p, '\0', '\0'};
        if (*p == '\\' || *p == '"') {
            encoded[0] = '\\'; encoded[1] = (char)*p;
        }
        if (!AppendText(buffer, size, used, encoded)) return FALSE;
    }
    return AppendText(buffer, size, used, "\"");
}

BOOL Statistics_SaveCategories(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\categories.json", g_statistics.directory);
    char json[32768] = {0};
    size_t used = 0;
    if (!AppendText(json, sizeof(json), &used,
                    "{\"schema_version\":1,\"selected_id\":")) return FALSE;
    AppendString(json, sizeof(json), &used,
                 g_statistics.categories[g_statistics.selected_category].id);
    AppendText(json, sizeof(json), &used, ",\"categories\":[");
    for (int i = 0; i < g_statistics.category_count; ++i) {
        StatisticsCategory* item = &g_statistics.categories[i];
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "%s{\"id\":", i ? "," : "");
        AppendText(json, sizeof(json), &used, prefix);
        AppendString(json, sizeof(json), &used, item->id);
        AppendText(json, sizeof(json), &used, ",\"name\":");
        AppendString(json, sizeof(json), &used, item->name);
        AppendText(json, sizeof(json), &used, ",\"color\":");
        AppendString(json, sizeof(json), &used, item->color);
        snprintf(prefix, sizeof(prefix), ",\"enabled\":%s,\"order\":%d}",
                 item->enabled ? "true" : "false", item->order);
        AppendText(json, sizeof(json), &used, prefix);
    }
    AppendText(json, sizeof(json), &used, "]}\n");
    return Statistics_AtomicReplace(path, json);
}

BOOL Statistics_LoadCategories(void) {
    SetGeneral();
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\categories.json", g_statistics.directory);
    wchar_t pathWide[MAX_PATH];
    FILE* file = NULL;
    if (!Statistics_Utf8PathToWide(path, pathWide, MAX_PATH) ||
        _wfopen_s(&file, pathWide, L"rb") != 0 || !file) {
        return Statistics_SaveCategories();
    }
    char json[32768] = {0};
    size_t count = fread(json, 1, sizeof(json) - 1, file);
    fclose(file);
    json[count] = '\0';
    int64_t schema;
    if (!Statistics_ReadJsonInt64(json, "schema_version", &schema) || schema != 1) {
        return FALSE;
    }
    char selected[STATISTICS_CATEGORY_ID_MAX] = "general";
    Statistics_ReadJsonString(json, "selected_id", selected, sizeof(selected));
    const char* cursor = strstr(json, "\"categories\"");
    int loaded = 0;
    while (cursor && loaded < STATISTICS_MAX_CATEGORIES) {
        cursor = strstr(cursor, "\"id\"");
        if (!cursor) break;
        StatisticsCategory category = {0};
        if (!Statistics_ReadJsonString(cursor, "id", category.id, sizeof(category.id)) ||
            !Statistics_ReadJsonString(cursor, "name", category.name, sizeof(category.name)) ||
            !Statistics_ReadJsonString(cursor, "color", category.color, sizeof(category.color)) ||
            !ValidUtf8(category.name)) {
            cursor += 4;
            continue;
        }
        const char* enabled = strstr(cursor, "\"enabled\":");
        const char* nextId = strstr(cursor + 4, "\"id\"");
        category.enabled = !(enabled && (!nextId || enabled < nextId) &&
                             strncmp(enabled + 10, "false", 5) == 0);
        category.order = loaded;
        g_statistics.categories[loaded++] = category;
        cursor += 4;
    }
    if (loaded == 0 || strcmp(g_statistics.categories[0].id, "general") != 0) {
        SetGeneral();
        return FALSE;
    }
    g_statistics.category_count = loaded;
    int index = FindCategory(selected);
    g_statistics.selected_category = index >= 0 ? index : 0;
    return TRUE;
}

int Statistics_GetCategories(StatisticsCategory* output, int capacity) {
    int count = g_statistics.category_count;
    if (output && capacity > 0) {
        if (count > capacity) count = capacity;
        memcpy(output, g_statistics.categories, count * sizeof(*output));
    }
    return g_statistics.category_count;
}

BOOL Statistics_GetSelectedCategory(StatisticsCategory* output) {
    if (!output || g_statistics.category_count <= 0) return FALSE;
    *output = g_statistics.categories[g_statistics.selected_category];
    return TRUE;
}

BOOL Statistics_SelectCategory(const char* id) {
    int index = FindCategory(id);
    if (index < 0 || !g_statistics.categories[index].enabled) return FALSE;
    CategorySnapshot snapshot;
    TakeSnapshot(&snapshot);
    g_statistics.selected_category = index;
    if (!CommitOrRestore(&snapshot)) return FALSE;
    Statistics_UpdateRuntimeExport(TRUE);
    return TRUE;
}

BOOL Statistics_AddCategory(const char* name, const char* color) {
    if (g_statistics.category_count >= STATISTICS_MAX_CATEGORIES ||
        !NameAvailable(name, -1)) return FALSE;
    CategorySnapshot snapshot;
    TakeSnapshot(&snapshot);
    StatisticsCategory* category =
        &g_statistics.categories[g_statistics.category_count];
    memset(category, 0, sizeof(*category));
    uint64_t candidate = (uint64_t)Statistics_UtcNowMs();
    if (candidate < g_statistics.next_id) candidate = g_statistics.next_id;
    snprintf(category->id, sizeof(category->id), "category_%llu",
             (unsigned long long)candidate);
    while (FindCategory(category->id) >= 0) {
        snprintf(category->id, sizeof(category->id), "category_%llu",
                 (unsigned long long)++candidate);
    }
    if (candidate >= g_statistics.next_id) g_statistics.next_id = candidate + 1;
    strcpy_s(category->name, sizeof(category->name), name);
    const char* selectedColor = CATEGORY_COLORS[
        g_statistics.category_count % _countof(CATEGORY_COLORS)];
    COLORREF parsed;
    char normalized[STATISTICS_COLOR_MAX];
    if (color && color[0] && ColorStringToColorRef(color, &parsed)) {
        normalizeColor(color, normalized, sizeof(normalized));
        selectedColor = normalized;
    }
    strcpy_s(category->color, sizeof(category->color), selectedColor);
    category->enabled = TRUE;
    category->order = g_statistics.category_count;
    g_statistics.category_count++;
    return CommitOrRestore(&snapshot);
}

BOOL Statistics_RenameCategory(const char* id, const char* name) {
    int index = FindCategory(id);
    if (index < 0 || !NameAvailable(name, index)) return FALSE;
    CategorySnapshot snapshot;
    TakeSnapshot(&snapshot);
    strcpy_s(g_statistics.categories[index].name,
             sizeof(g_statistics.categories[index].name), name);
    return CommitOrRestore(&snapshot);
}

BOOL Statistics_DeleteCategory(const char* id) {
    int index = FindCategory(id);
    if (index <= 0) return FALSE;
    CategorySnapshot snapshot;
    TakeSnapshot(&snapshot);
    for (int i = index; i + 1 < g_statistics.category_count; ++i) {
        g_statistics.categories[i] = g_statistics.categories[i + 1];
        g_statistics.categories[i].order = i;
    }
    g_statistics.category_count--;
    if (g_statistics.selected_category == index) g_statistics.selected_category = 0;
    else if (g_statistics.selected_category > index) g_statistics.selected_category--;
    return CommitOrRestore(&snapshot);
}

BOOL Statistics_SetCategoryColor(const char* id, const char* color) {
    int index = FindCategory(id);
    COLORREF parsed;
    if (index < 0 || !color || !ColorStringToColorRef(color, &parsed)) return FALSE;
    CategorySnapshot snapshot;
    TakeSnapshot(&snapshot);
    char normalized[STATISTICS_COLOR_MAX];
    normalizeColor(color, normalized, sizeof(normalized));
    strcpy_s(g_statistics.categories[index].color,
             sizeof(g_statistics.categories[index].color), normalized);
    return CommitOrRestore(&snapshot);
}

BOOL Statistics_MoveCategory(const char* id, int direction) {
    int index = FindCategory(id);
    int target = index + direction;
    if (index <= 0 || target <= 0 || target >= g_statistics.category_count) {
        return FALSE;
    }
    CategorySnapshot snapshot;
    TakeSnapshot(&snapshot);
    StatisticsCategory swap = g_statistics.categories[index];
    g_statistics.categories[index] = g_statistics.categories[target];
    g_statistics.categories[target] = swap;
    g_statistics.categories[index].order = index;
    g_statistics.categories[target].order = target;
    if (g_statistics.selected_category == index) g_statistics.selected_category = target;
    else if (g_statistics.selected_category == target) g_statistics.selected_category = index;
    return CommitOrRestore(&snapshot);
}
