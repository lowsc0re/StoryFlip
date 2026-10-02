#include "sf_library.h"
#include <string.h>
#include <ctype.h>
#include <limits.h>
static bool matches(const SfRecord* r, uint32_t type, uint32_t id, const char* path) {
    if(!(r->flags & type)) return false;
    if(type == SfCategory) return r->order == id;
    if(type == SfLast) return true;
    if(type == SfFolder) return path && !strcmp(r->path, path);
    return r->order == id && path && !strcmp(sf_name(r->path), sf_name(path));
}
bool sf_library_get(SfStore* s, uint32_t type, uint32_t id, const char* path, SfRecord* out) {
    File* f = sf_store_open(s); SfRecord r; bool found = false;
    while(sf_record_read(f, &r)) if(matches(&r, type, id, path)) { *out = r; found = true; break; }
    sf_close(f); return found;
}
uint32_t sf_library_count(SfStore* s, uint32_t type, uint32_t id) {
    File* f = sf_store_open(s); SfRecord r; uint32_t n = 0;
    while(sf_record_read(f, &r)) if((r.flags & type) && (type != SfMember || r.order == id)) n++;
    sf_close(f); return n;
}
bool sf_library_nth(SfStore* s, uint32_t type, uint32_t index, SfRecord* out) {
    File* f = sf_store_open(s); SfRecord r; bool found = false;
    while(sf_record_read(f, &r)) if((r.flags & type) && index-- == 0) { *out = r; found = true; break; }
    sf_close(f); return found;
}
bool sf_library_put(SfStore* s, const SfRecord* value, bool remove) {
    uint32_t type = value->flags & (SfCategory | SfMember | SfFolder | SfLast);
    if(!type || (type & (type - 1))) return false;
    File* f = sf_store_open(s); if(s->active >= 0 && !f) return false;
    SfWriter w; sf_writer_begin(&w, s); SfRecord r;
    for(uint32_t i = 0; i < s->header.count; i++) {
        if(!sf_record_read(f, &r)) { sf_close(f); sf_writer_abort(&w); return false; }
        if(!matches(&r, type, value->order, value->path)) sf_writer_add(&w, &r);
    }
    sf_close(f); if(!remove) sf_writer_add(&w, value);
    return sf_writer_finish(&w);
}
bool sf_library_category(SfStore* s, uint32_t id, const char* name) {
    size_t len = strlen(name);
    if(!len || len >= 64) return false;
    bool visible = false;
    for(size_t i = 0; i < len; i++) { if((unsigned char)name[i] < 32 || name[i] == '/') return false; if(!isspace((unsigned char)name[i])) visible = true; }
    if(!visible) return false;
    File* f = sf_store_open(s); SfRecord r; uint32_t max_id = 0; bool exists = false, duplicate = false;
    while(sf_record_read(f, &r)) if(r.flags & SfCategory) {
        if(r.order > max_id) max_id = r.order;
        if(r.order == id) exists = true;
        if(r.order != id && !strcmp(r.path + 1, name)) duplicate = true;
    }
    sf_close(f); if(duplicate || (id && !exists) || (!id && max_id == UINT32_MAX)) return false;
    memset(&r, 0, sizeof(r)); r.flags = SfCategory; r.order = id ? id : max_id + 1;
    r.path[0] = '/'; sf_copy(r.path + 1, SF_PATH - 1, name); return sf_library_put(s, &r, false);
}
static bool remove_group(SfStore* s, uint32_t id, bool folders) {
    File* f = sf_store_open(s); if(s->active >= 0 && !f) return false;
    SfWriter w; sf_writer_begin(&w, s); SfRecord r;
    for(uint32_t i = 0; i < s->header.count; i++) {
        if(!sf_record_read(f, &r)) { sf_close(f); sf_writer_abort(&w); return false; }
        bool remove = folders ? (r.flags & SfFolder) != 0 : (r.flags & (SfCategory | SfMember)) && r.order == id;
        if(!remove) sf_writer_add(&w, &r);
    }
    sf_close(f); return sf_writer_finish(&w);
}
bool sf_library_delete_category(SfStore* s, uint32_t id) { return remove_group(s, id, false); }
bool sf_library_clear_folders(SfStore* s) { return remove_group(s, 0, true); }
bool sf_library_rebase(SfStore* s, SfStore* search) {
    if(s->active < 0) return true;
    File* f = sf_store_open(s); if(!f) return false;
    SfWriter w; sf_writer_begin(&w, s); SfRecord r, match;
    for(uint32_t i = 0; i < s->header.count; i++) {
        if(!sf_record_read(f, &r)) { sf_close(f); sf_writer_abort(&w); return false; }
        if((r.flags & (SfMember | SfLast)) && sf_store_find(search, sf_name(r.path), &match) && !(match.flags & SF_DIRECTORY)) sf_copy(r.path, SF_PATH, match.path);
        sf_writer_add(&w, &r);
    }
    sf_close(f); return sf_writer_finish(&w);
}
