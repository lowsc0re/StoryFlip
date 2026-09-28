#pragma once
#include "sf_store.h"
#define SF_BROWSER_SLOTS 3
#define SF_BROWSER_BUDGET (24 * 1024)
typedef struct {
    char path[SF_PATH];
    char* data;
    uint32_t size, capacity, count, selected, stamp, meta_generation;
    bool valid, directories_only, streaming, has_files;
} SfDirectory;
typedef struct {
    SfDirectory dirs[SF_BROWSER_SLOTS];
    int active;
    uint32_t stamp;
    Storage* storage;
} SfBrowser;
void sf_browser_init(SfBrowser* b, Storage* storage);
void sf_browser_clear(SfBrowser* b);
bool sf_browser_open(SfBrowser* b, const char* path, bool directories_only, uint32_t* selection);
bool sf_browser_get(SfBrowser* b, uint32_t index, SfRecord* r);
uint32_t sf_browser_count(SfBrowser* b);
void sf_browser_remember(SfBrowser* b, uint32_t selected);
void sf_browser_invalidate(SfBrowser* b, const char* path);

bool sf_browser_status(SfBrowser* b, SfStore* metadata);
