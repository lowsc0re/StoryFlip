#include "sf_store.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

_Static_assert(sizeof(SfRecord) == 1040, "disk record layout");
_Static_assert(sizeof(SfHeader) == 24, "disk header layout");

static uint32_t sf_crc(uint32_t crc, const void* data, size_t size) {
    const uint8_t* p = data;
    while(size--) {
        crc ^= *p++;
        for(unsigned i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}
const char* sf_name(const char* path) {
    const char* p = strrchr(path, '/');
    return p ? p + 1 : path;
}
bool sf_copy(char* dst, size_t size, const char* src) {
    if(strlen(src) >= size) return false;
    memmove(dst, src, strlen(src) + 1);
    return true;
}
bool sf_join(char* dst, size_t size, const char* parent, const char* name) {
    size_t p = strlen(parent), n = strlen(name);
    bool slash = p && parent[p - 1] != '/';
    if(p + slash + n >= size) return false;
    memcpy(dst, parent, p);
    if(slash) dst[p++] = '/';
    memcpy(dst + p, name, n + 1);
    return true;
}
bool sf_is_nfc(const char* name) {
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".nfc") == 0;
}
void sf_close(File* f) {
    if(f) {
        storage_file_close(f);
        storage_file_free(f);
    }
}
bool sf_record_read(File* f, SfRecord* r) {
    return f && storage_file_read(f, r, sizeof(*r)) == sizeof(*r) &&
           memchr(r->path, 0, sizeof(r->path)) && r->path[0] == '/' &&
           SF_RATING(r) <= 5;
}
bool sf_record_write(File* f, const SfRecord* r) {
    return storage_file_write(f, r, sizeof(*r)) == sizeof(*r);
}
static bool sf_validate(SfStore* s, int slot, SfHeader* h) {
    File* f = storage_file_alloc(s->storage);
    bool ok = storage_file_open(f, s->paths[slot], FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) ok = storage_file_read(f, h, sizeof(*h)) == sizeof(*h);
    if(ok) ok = h->magic == s->magic && h->version == 1 && h->generation != 0 &&
                 h->seal == sf_crc(UINT32_MAX, h, offsetof(SfHeader, seal)) &&
                 storage_file_size(f) == sizeof(*h) + (uint64_t)h->count * sizeof(SfRecord);
    uint32_t hash = UINT32_MAX;
    SfRecord r;
    for(uint32_t i = 0; ok && i < h->count; i++) {
        ok = sf_record_read(f, &r);
        hash = sf_crc(hash, &r, sizeof(r));
    }
    if(ok) ok = hash == h->hash;
    sf_close(f);
    return ok;
}
void sf_store_init(SfStore* s, Storage* storage, const char* a, const char* b, uint32_t magic) {
    memset(s, 0, sizeof(*s));
    s->storage = storage; s->paths[0] = a; s->paths[1] = b; s->magic = magic; s->active = -1;
    for(int i = 0; i < 2; i++) {
        SfHeader h;
        if(sf_validate(s, i, &h)) {
            if(s->active < 0 || h.generation > s->header.generation) {
                s->active = i; s->header = h;
            }
        } else if(storage_common_stat(storage, s->paths[i], NULL) == FSE_OK) s->damaged = true;
    }
}
File* sf_store_open(SfStore* s) {
    if(s->active < 0) return NULL;
    File* f = storage_file_alloc(s->storage);
    if(!storage_file_open(f, s->paths[s->active], FSAM_READ, FSOM_OPEN_EXISTING) ||
       !storage_file_seek(f, sizeof(SfHeader), true)) {
        sf_close(f); return NULL;
    }
    return f;
}
bool sf_store_find(SfStore* s, const char* name, SfRecord* r) {
    File* f = sf_store_open(s);
    bool found = false;
    while(sf_record_read(f, r)) {
        if(strcmp(sf_name(r->path), name) == 0) { found = true; break; }
    }
    sf_close(f);
    return found;
}
void sf_writer_begin(SfWriter* w, SfStore* s) {
    memset(w, 0, sizeof(*w));
    w->store = s; w->slot = s->active == 0 ? 1 : 0;
    w->header.magic = s->magic; w->header.version = 1;
    w->header.generation = s->header.generation + 1;
    w->header.hash = UINT32_MAX;
    w->file = storage_file_alloc(s->storage);
    w->ok = w->header.generation != 0 &&
            storage_file_open(w->file, s->paths[w->slot], FSAM_WRITE, FSOM_CREATE_ALWAYS);
    SfHeader empty = {0};
    if(w->ok) w->ok = storage_file_write(w->file, &empty, sizeof(empty)) == sizeof(empty);
}
bool sf_writer_add(SfWriter* w, const SfRecord* r) {
    if(!w->ok) return false;
    if(w->header.count >= (UINT32_MAX - sizeof(SfHeader)) / sizeof(SfRecord)) w->ok = false;
    if(w->ok) w->ok = sf_record_write(w->file, r);
    if(w->ok) {
        w->header.hash = sf_crc(w->header.hash, r, sizeof(*r));
        w->header.count++;
    }
    return w->ok;
}
void sf_writer_abort(SfWriter* w) {
    sf_close(w->file); w->file = NULL; w->ok = false;
    storage_common_remove(w->store->storage, w->store->paths[w->slot]);
}
bool sf_writer_finish(SfWriter* w) {
    if(w->ok) {
        w->header.seal = sf_crc(UINT32_MAX, &w->header, offsetof(SfHeader, seal));
        w->ok = storage_file_seek(w->file, 0, true) &&
                storage_file_write(w->file, &w->header, sizeof(w->header)) == sizeof(w->header) &&
                storage_file_sync(w->file);
    }
    sf_close(w->file); w->file = NULL;
    SfHeader check;
    if(w->ok) w->ok = sf_validate(w->store, w->slot, &check);
    if(w->ok) { w->store->active = w->slot; w->store->header = check; }
    else sf_writer_abort(w);
    return w->ok;
}
// clear: 0 upsert, 1 favorites, 2 ratings, 3 recent, 4 missing paths.
bool sf_store_update(SfStore* s, const SfRecord* value, int clear) {
    SfWriter w;
    sf_writer_begin(&w, s);
    File* src = sf_store_open(s);
    if(s->active >= 0 && !src) w.ok = false;
    SfRecord r;
    bool found = false;
    for(uint32_t i = 0; w.ok && i < s->header.count; i++) {
        if(!sf_record_read(src, &r)) { w.ok = false; break; }
        if(value && strcmp(sf_name(r.path), sf_name(value->path)) == 0) {
            r = *value; found = true;
        }
        if(clear == 1) r.flags &= ~SF_FAVORITE;
        if(clear == 2) SF_SET_RATING(&r, 0);
        if(clear == 3) { r.last = 0; r.order = 0; }
        if(clear == 4 && storage_common_stat(s->storage, r.path, NULL) == FSE_NOT_EXIST) continue;
        sf_writer_add(&w, &r);
    }
    if(value && !found) sf_writer_add(&w, value);
    sf_close(src);
    return sf_writer_finish(&w);
}

typedef struct { uint64_t hash; uint32_t index; bool used; } SfNameRef;
static uint64_t name_hash(const char* s) {
    uint64_t h = UINT64_C(14695981039346656037);
    while(*s) { h ^= (uint8_t)*s++; h *= UINT64_C(1099511628211); }
    return h;
}
bool sf_store_rebase(SfStore* metadata, SfStore* search) {
    uint32_t count = metadata->header.count;
    if(!count) return true;
    if(count > UINT32_MAX / sizeof(SfNameRef)) return false;
    SfNameRef* names = calloc(count, sizeof(SfNameRef));
    if(!names) return false;
    File* source = sf_store_open(metadata);
    File* paths = sf_store_open(search);
    bool ok = source && paths;
    SfRecord r, found;
    for(uint32_t i = 0; ok && i < count; i++) {
        ok = sf_record_read(source, &r);
        if(ok) { names[i].hash = name_hash(sf_name(r.path)); names[i].index = i; }
    }
    SfWriter writer; sf_writer_begin(&writer, metadata);
    for(uint32_t j = 0; ok && j < search->header.count; j++) {
        ok = sf_record_read(paths, &found);
        if(!ok || (found.flags & SF_DIRECTORY)) continue;
        uint64_t hash = name_hash(sf_name(found.path));
        for(uint32_t i = 0; ok && i < count; i++) {
            if(names[i].used || names[i].hash != hash) continue;
            ok = storage_file_seek(source, sizeof(SfHeader) + names[i].index * sizeof(r), true) && sf_record_read(source, &r);
            if(ok && !strcmp(sf_name(r.path), sf_name(found.path))) {
                sf_copy(r.path, SF_PATH, found.path); names[i].used = true;
                ok = sf_writer_add(&writer, &r); break;
            }
        }
    }
    for(uint32_t i = 0; ok && i < count; i++) {
        if(names[i].used) continue;
        ok = storage_file_seek(source, sizeof(SfHeader) + names[i].index * sizeof(r), true) && sf_record_read(source, &r) && sf_writer_add(&writer, &r);
    }
    sf_close(source); sf_close(paths); free(names);
    if(!ok) { sf_writer_abort(&writer); return false; }
    return sf_writer_finish(&writer);
}
