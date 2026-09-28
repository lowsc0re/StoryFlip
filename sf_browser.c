#include "sf_browser.h"
#include <stdlib.h>
#include <string.h>
static void release(SfDirectory* d) { free(d->data); memset(d, 0, sizeof(*d)); }
void sf_browser_init(SfBrowser* b, Storage* storage) {
    memset(b, 0, sizeof(*b)); b->storage = storage; b->active = -1;
}
void sf_browser_clear(SfBrowser* b) {
    for(unsigned i = 0; i < SF_BROWSER_SLOTS; i++) release(&b->dirs[i]);
    b->active = -1;
}
void sf_browser_invalidate(SfBrowser* b, const char* path) {
    for(unsigned i = 0; i < SF_BROWSER_SLOTS; i++) {
        if(b->dirs[i].valid && !strcmp(b->dirs[i].path, path)) {
            release(&b->dirs[i]); if(b->active == (int)i) b->active = -1;
        }
    }
}
static bool append(SfBrowser* b, SfDirectory* d, const char* name, bool dir) {
    if(d->streaming) return true;
    uint32_t need = d->size + strlen(name) + 2;
    if(need > SF_BROWSER_BUDGET) {
        free(d->data); d->data = NULL; d->capacity = d->size = 0; d->streaming = true;
        return true;
    }
    if(need > d->capacity) {
        uint32_t capacity = (need + 511) & ~511u;
        for(;;) {
            uint32_t total = capacity;
            int oldest = -1;
            for(unsigned i = 0; i < SF_BROWSER_SLOTS; i++) {
                SfDirectory* other = &b->dirs[i];
                if(other == d) continue;
                total += other->capacity;
                if(other->valid && (oldest < 0 || other->stamp < b->dirs[oldest].stamp)) oldest = i;
            }
            if(total <= SF_BROWSER_BUDGET) break;
            if(oldest < 0) return false;
            release(&b->dirs[oldest]);
        }
        char* p = realloc(d->data, capacity);
        if(!p) {
            free(d->data); d->data = NULL; d->capacity = d->size = 0; d->streaming = true;
            return true;
        }
        d->data = p; d->capacity = capacity;
    }
    d->data[d->size++] = dir ? 128 : 0;
    size_t n = strlen(name) + 1;
    memcpy(d->data + d->size, name, n); d->size += n;
    return true;
}
static bool walk(SfBrowser* b, SfDirectory* d, uint32_t wanted, SfRecord* result) {
    File* f = storage_file_alloc(b->storage);
    bool ok = true, found = false;
    uint32_t count = 0;
    FileInfo info; char name[SF_NAME];
    for(unsigned pass = 0; ok && !found && pass < (d->directories_only ? 1u : 2u); pass++) {
        if(!storage_dir_open(f, d->path)) { ok = false; break; }
        while(storage_dir_read(f, &info, name, sizeof(name))) {
            if(!strcmp(name, ".") || !strcmp(name, "..")) continue;
            bool dir = file_info_is_dir(&info);
            if((pass == 0) != dir || (!dir && !sf_is_nfc(name))) continue;
            if(strlen(d->path) + strlen(name) + 2 > SF_PATH) { ok = false; break; }
            if(result && count == wanted) {
                memset(result, 0, sizeof(*result));
                ok = sf_join(result->path, SF_PATH, d->path, name);
                result->flags = dir ? SF_DIRECTORY : 0; found = true; break;
            }
            if(!dir) d->has_files = true;
            if(!result && !append(b, d, name, dir)) { ok = false; break; }
            count++;
        }
        FS_Error error = storage_file_get_error(f);
        if(error != FSE_OK && error != FSE_NOT_EXIST) ok = false;
        storage_dir_close(f);
    }
    storage_file_free(f);
    if(!result) d->count = count;
    return ok && (!result || found);
}
bool sf_browser_open(SfBrowser* b, const char* path, bool only, uint32_t* selection) {
    int slot = -1;
    for(unsigned i = 0; i < SF_BROWSER_SLOTS; i++) {
        SfDirectory* d = &b->dirs[i];
        if(d->valid && d->directories_only == only && !strcmp(d->path, path)) {
            b->active = i; d->stamp = ++b->stamp; *selection = d->selected; return true;
        }
        if(slot < 0 || !d->valid || (b->dirs[slot].valid && d->stamp < b->dirs[slot].stamp)) slot = i;
    }
    SfDirectory* d = &b->dirs[slot]; release(d);
    if(!sf_copy(d->path, SF_PATH, path)) return false;
    d->directories_only = only;
    if(!walk(b, d, 0, NULL)) { release(d); b->active = -1; return false; }
    d->valid = true; d->stamp = ++b->stamp; b->active = slot; *selection = 0;
    return true;
}
bool sf_browser_get(SfBrowser* b, uint32_t index, SfRecord* r) {
    if(b->active < 0) return false;
    SfDirectory* d = &b->dirs[b->active];
    if(!d->valid || index >= d->count) return false;
    if(d->streaming) return walk(b, d, index, r);
    const char* p = d->data;
    while(index--) p += strlen(p + 1) + 2;
    memset(r, 0, sizeof(*r)); r->flags = ((uint8_t)*p & 128u) ? SF_DIRECTORY : (uint8_t)*p;
    return sf_join(r->path, SF_PATH, d->path, p + 1);
}
uint32_t sf_browser_count(SfBrowser* b) { return b->active < 0 ? 0 : b->dirs[b->active].count; }
void sf_browser_remember(SfBrowser* b, uint32_t selected) {
    if(b->active >= 0) b->dirs[b->active].selected = selected;
}

bool sf_browser_status(SfBrowser* b, SfStore* metadata) {
    if(b->active < 0) return false;
    SfDirectory* d = &b->dirs[b->active];
    if(d->streaming) return false;
    if(!d->has_files || d->meta_generation == metadata->header.generation) return true;
    for(char* p = d->data; p < d->data + d->size; p += strlen(p + 1) + 2)
        if(!((uint8_t)*p & 128u)) *p = 0;
    File* f = sf_store_open(metadata); SfRecord r;
    uint32_t read = 0;
    while(sf_record_read(f, &r)) {
        read++;
        for(char* p = d->data; p < d->data + d->size; p += strlen(p + 1) + 2)
            if(!((uint8_t)*p & 128u) && !strcmp(p + 1, sf_name(r.path))) *p = r.flags & 127u;
    }
    sf_close(f);
    if(read != metadata->header.count) return false;
    d->meta_generation = metadata->header.generation;
    return true;
}
