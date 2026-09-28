#include "sf_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
struct File { FILE* fp; DIR* dir; FS_Error error; char path[SF_PATH]; };
static unsigned nfc_reads, file_opens, file_writes, dir_opens;
static long write_budget = -1;
static bool sync_fail;
File* storage_file_alloc(Storage* s) { (void)s; return calloc(1, sizeof(File)); }
void storage_file_free(File* f) { free(f); }
bool storage_file_open(File* f, const char* p, FS_AccessMode m, FS_OpenMode o) {
    file_opens++;
    if(sf_is_nfc(p)) nfc_reads++;
    f->fp = fopen(p, o == FSOM_CREATE_ALWAYS ? "wb+" : m == FSAM_READ ? "rb" : "rb+"); return f->fp != NULL;
}
bool storage_file_close(File* f) { if(f->fp) fclose(f->fp); f->fp = NULL; return true; }
size_t storage_file_read(File* f, void* p, size_t n) { return f->fp ? fread(p, 1, n, f->fp) : 0; }
size_t storage_file_write(File* f, const void* p, size_t n) {
    file_writes++;
    if(!f->fp) return 0;
    if(write_budget >= 0) { if(n > (size_t)write_budget) n = write_budget; write_budget -= n; }
    return fwrite(p, 1, n, f->fp);
}
bool storage_file_seek(File* f, uint32_t p, bool start) { return f->fp && fseek(f->fp, p, start ? SEEK_SET : SEEK_CUR) == 0; }
uint64_t storage_file_size(File* f) { long p = ftell(f->fp); fseek(f->fp, 0, SEEK_END); long n = ftell(f->fp); fseek(f->fp, p, SEEK_SET); return n; }
bool storage_file_sync(File* f) { return !sync_fail && fflush(f->fp) == 0; }
FS_Error storage_common_stat(Storage* s, const char* p, void* info) { (void)s; struct stat st; if(stat(p, &st) != 0) return errno == ENOENT ? FSE_NOT_EXIST : FSE_INTERNAL; if(info) ((FileInfo*)info)->dir = S_ISDIR(st.st_mode); return FSE_OK; }
bool storage_dir_open(File* f, const char* path) { dir_opens++; f->dir = opendir(path); sf_copy(f->path, SF_PATH, path); f->error = f->dir ? FSE_OK : FSE_NOT_EXIST; return f->dir != NULL; }
bool storage_dir_close(File* f) { if(f->dir) closedir(f->dir); f->dir = NULL; return true; }
bool storage_dir_read(File* f, FileInfo* info, char* name, uint16_t size) {
    struct dirent* e = readdir(f->dir); if(!e) { f->error = FSE_NOT_EXIST; return false; }
    sf_copy(name, size, e->d_name); char p[SF_PATH]; assert(sf_join(p, SF_PATH, f->path, name));
    f->error = storage_common_stat(NULL, p, info); return f->error == FSE_OK;
}
FS_Error storage_file_get_error(File* f) { return f->error; }
FS_Error storage_common_remove(Storage* s, const char* path) { (void)s; return unlink(path) == 0 ? FSE_OK : FSE_NOT_EXIST; }
#ifndef SF_ADAPTER_ONLY
static Storage storage;
static SfStore load(void) { SfStore s; sf_store_init(&s, &storage, "meta.a", "meta.b", 0x53464d45); return s; }
static void clean(void) { unlink("meta.a"); unlink("meta.b"); }
int main(int argc, char** argv) {
    assert(argc == 2); assert(chdir(argv[1]) == 0); clean();
    SfStore s = load(); assert(s.active == -1);
    SfRecord r = {0}, out; strcpy(r.path, "/ext/nfc/StoryFlip/Group/Test.nfc"); r.plays = 1; r.order = 1;
    assert(sf_store_update(&s, &r, 0));
    s = load(); assert(s.header.count == 1 && sf_store_find(&s, "Test.nfc", &out) && out.plays == 1);
    strcpy(r.path, "/ext/nfc/StoryFlip/Other/Test.nfc"); r.plays = 2; r.flags = SF_FAVORITE; SF_SET_RATING(&r, 5);
    assert(sf_store_update(&s, &r, 0)); assert(s.header.count == 1);
    assert(sf_store_find(&s, "Test.nfc", &out) && out.plays == 2 && !strcmp(out.path, r.path));
    assert(SF_RATING(&out) == 5);
    // Every cut point through the complete two-record commit is recoverable.
    SfRecord second = {0}; strcpy(second.path, "/ext/nfc/StoryFlip/Second.nfc"); second.plays = 7;
    for(long cut = 0; cut < (long)(2 * sizeof(SfHeader) + 2 * sizeof(SfRecord)); cut++) {
        write_budget = cut;
        assert(!sf_store_update(&s, &second, 0));
        write_budget = -1;
        SfStore recovered = load();
        assert(recovered.header.count == 1);
        assert(sf_store_find(&recovered, "Test.nfc", &out) && out.plays == 2);
    }
    puts("PASS: 2128 interrupted-write positions preserve previous committed data");
    assert(sf_store_update(&s, &second, 0));
    assert(sf_store_update(&s, NULL, 1)); assert(sf_store_find(&s, "Test.nfc", &out) && !(out.flags & SF_FAVORITE) && SF_RATING(&out) == 5);
    assert(sf_store_update(&s, NULL, 2)); assert(sf_store_find(&s, "Test.nfc", &out) && !SF_RATING(&out) && out.plays == 2);
    assert(sf_store_update(&s, NULL, 3)); assert(sf_store_find(&s, "Test.nfc", &out) && out.order == 0 && out.plays == 2);
    puts("PASS: filename identity, moved paths and independent reset operations");
    // Corrupt active payload; loader must choose the older valid slot.
    unsigned previous_generation = s.header.generation - 1;
    FILE* bad = fopen(s.paths[s.active], "rb+"); fseek(bad, sizeof(SfHeader) + 60, SEEK_SET); fputc(0x77, bad); fclose(bad);
    s = load(); assert(s.damaged && s.header.generation == previous_generation);
    puts("PASS: payload CRC rejects corrupt latest generation");
    clean(); s = load();
    SfWriter w; sf_writer_begin(&w, &s);
    for(unsigned i = 0; i < 2000; i++) {
        memset(&r, 0, sizeof(r)); snprintf(r.path, SF_PATH, "/ext/nfc/StoryFlip/G/Title %u.nfc", i);
        r.plays = i; assert(sf_writer_add(&w, &r));
    }
    assert(sf_writer_finish(&w)); s = load();
    assert(s.header.count == 2000 && sf_store_find(&s, "Title 1999.nfc", &out) && out.plays == 1999);
    char dest[SF_PATH]; char name[SF_NAME]; memset(name, 'x', SF_NAME - 1); name[SF_NAME - 1] = 0;
    assert(sf_join(dest, sizeof(dest), "/ext/nfc/StoryFlip", name));
    assert(!sf_copy(dest, 8, name)); assert(!sf_join(dest, 8, "/ext", "toolong"));
    puts("PASS: 2000 entries and explicit overlength handling");
    clean();
    return 0;
}

#endif
