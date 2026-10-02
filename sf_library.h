#pragma once
#include "sf_store.h"
enum { SfCategory = 0x1000, SfMember = 0x2000, SfFolder = 0x4000, SfLast = 0x8000 };
bool sf_library_get(SfStore* s, uint32_t type, uint32_t id, const char* path, SfRecord* out);
uint32_t sf_library_count(SfStore* s, uint32_t type, uint32_t id);
bool sf_library_nth(SfStore* s, uint32_t type, uint32_t index, SfRecord* out);
bool sf_library_put(SfStore* s, const SfRecord* value, bool remove);
bool sf_library_category(SfStore* s, uint32_t id, const char* name);
bool sf_library_delete_category(SfStore* s, uint32_t id);
bool sf_library_clear_folders(SfStore* s);
bool sf_library_rebase(SfStore* s, SfStore* search);
