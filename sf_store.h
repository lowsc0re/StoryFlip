#pragma once
#include <furi.h>
#include <storage/storage.h>
#define SF_PATH 1024
#define SF_NAME 256
#define SF_FAVORITE 1u
#define SF_DIRECTORY 256u
#define SF_RATING(r) (((r)->flags >> 4) & 7u)
#define SF_SET_RATING(r, v) ((r)->flags = ((r)->flags & ~112u) | ((v) << 4))
typedef struct {
    char path[SF_PATH];
    uint32_t plays;
    uint32_t last;
    uint32_t order;
    uint32_t flags;
} SfRecord;
typedef struct {
    uint32_t magic, version, generation, count, hash, seal;
} SfHeader;
typedef struct {
    Storage* storage;
    const char* paths[2];
    uint32_t magic;
    int active;
    SfHeader header;
    bool damaged;
} SfStore;
typedef struct {
    SfStore* store;
    File* file;
    SfHeader header;
    int slot;
    bool ok;
} SfWriter;
const char* sf_name(const char* path);
bool sf_copy(char* dst, size_t size, const char* src);
bool sf_join(char* dst, size_t size, const char* parent, const char* name);
bool sf_is_nfc(const char* name);
void sf_store_init(SfStore* s, Storage* storage, const char* a, const char* b, uint32_t magic);
File* sf_store_open(SfStore* s);
bool sf_record_read(File* f, SfRecord* record);
bool sf_record_write(File* f, const SfRecord* record);
bool sf_store_find(SfStore* s, const char* name, SfRecord* record);
void sf_writer_begin(SfWriter* w, SfStore* store);
bool sf_writer_add(SfWriter* w, const SfRecord* record);
bool sf_writer_finish(SfWriter* w);
void sf_writer_abort(SfWriter* w);
bool sf_store_update(SfStore* s, const SfRecord* value, int clear);
void sf_close(File* file);
bool sf_store_rebase(SfStore* metadata, SfStore* search);
