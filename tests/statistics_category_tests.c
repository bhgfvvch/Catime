#include "statistics_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#undef assert
#define assert(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Assertion failed: %s (%s:%d)\n", \
                #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

StatisticsState g_statistics = {0};
static char s_lastJson[32768];
static BOOL s_saveSucceeds = TRUE;

BOOL Statistics_AtomicReplace(const char* path, const char* content) {
    (void)path;
    strcpy_s(s_lastJson, sizeof(s_lastJson), content);
    return s_saveSucceeds;
}

BOOL Statistics_Utf8PathToWide(const char* path, wchar_t* output, size_t size) {
    (void)path; (void)output; (void)size;
    return FALSE;
}

void Statistics_UpdateRuntimeExport(BOOL force) { (void)force; }
int64_t Statistics_UtcNowMs(void) { return 1787554800000LL; }

static void InitializeGeneral(void) {
    memset(&g_statistics, 0, sizeof(g_statistics));
    strcpy_s(g_statistics.directory, sizeof(g_statistics.directory), "test");
    strcpy_s(g_statistics.categories[0].id,
             sizeof(g_statistics.categories[0].id), "general");
    strcpy_s(g_statistics.categories[0].name,
             sizeof(g_statistics.categories[0].name), "General");
    strcpy_s(g_statistics.categories[0].color,
             sizeof(g_statistics.categories[0].color), "#3A96DD");
    g_statistics.categories[0].enabled = TRUE;
    g_statistics.category_count = 1;
}

int main(void) {
    InitializeGeneral();
    assert(Statistics_AddCategory("精读", "#57A773"));
    assert(Statistics_AddCategory("背单词", "#E0A458"));
    assert(g_statistics.category_count == 3);
    assert(strstr(s_lastJson, "精读") != NULL);
    assert(Statistics_SelectCategory(g_statistics.categories[1].id));
    assert(strcmp(g_statistics.categories[g_statistics.selected_category].name,
                  "精读") == 0);
    s_saveSucceeds = FALSE;
    assert(!Statistics_SelectCategory("general"));
    assert(strcmp(g_statistics.categories[g_statistics.selected_category].name,
                  "精读") == 0);
    assert(!Statistics_AddCategory("临时", "#000000"));
    assert(g_statistics.category_count == 3);
    assert(!Statistics_RenameCategory(g_statistics.categories[1].id, "临时"));
    assert(strcmp(g_statistics.categories[1].name, "精读") == 0);
    assert(!Statistics_SetCategoryColor(g_statistics.categories[1].id, "#123456"));
    assert(strcmp(g_statistics.categories[1].color, "#57A773") == 0);
    assert(!Statistics_MoveCategory(g_statistics.categories[2].id, -1));
    assert(strcmp(g_statistics.categories[1].name, "精读") == 0);
    assert(!Statistics_DeleteCategory(g_statistics.categories[1].id));
    assert(g_statistics.category_count == 3);
    s_saveSucceeds = TRUE;
    assert(Statistics_RenameCategory(g_statistics.categories[1].id, "阅读"));
    assert(!Statistics_RenameCategory(g_statistics.categories[1].id, "背单词"));
    assert(Statistics_SetCategoryColor(g_statistics.categories[1].id, "#123456"));
    assert(strcmp(g_statistics.categories[1].color, "#123456") == 0);
    assert(Statistics_MoveCategory(g_statistics.categories[2].id, -1));
    assert(strcmp(g_statistics.categories[1].name, "背单词") == 0);
    assert(Statistics_DeleteCategory(g_statistics.categories[2].id));
    assert(g_statistics.category_count == 2);
    assert(g_statistics.selected_category == 0);
    assert(!Statistics_DeleteCategory("general"));
    assert(!Statistics_AddCategory("bad\nname", "#000000"));
    puts("statistics_category_tests: PASS");
    return 0;
}
