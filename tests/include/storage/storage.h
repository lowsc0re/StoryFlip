#pragma once
#include <furi.h>
typedef struct Storage { int unused; } Storage;
typedef struct File File;
typedef struct { bool dir; } FileInfo;
static inline bool file_info_is_dir(const FileInfo* i) { return i->dir; }
typedef enum { FSE_OK, FSE_NOT_EXIST, FSE_INTERNAL } FS_Error;
typedef enum { FSAM_READ, FSAM_WRITE, FSAM_READ_WRITE } FS_AccessMode;
typedef enum { FSOM_OPEN_EXISTING, FSOM_CREATE_ALWAYS } FS_OpenMode;
File* storage_file_alloc(Storage* s);
void storage_file_free(File* f);
bool storage_file_open(File* f, const char* path, FS_AccessMode mode, FS_OpenMode open);
bool storage_file_close(File* f);
size_t storage_file_read(File* f, void* data, size_t size);
size_t storage_file_write(File* f, const void* data, size_t size);
bool storage_file_seek(File* f, uint32_t offset, bool from_start);
uint64_t storage_file_size(File* f);
bool storage_file_sync(File* f);
FS_Error storage_common_stat(Storage* s, const char* path, void* info);

bool storage_dir_open(File* f, const char* path);
bool storage_dir_close(File* f);
bool storage_dir_read(File* f, FileInfo* info, char* name, uint16_t size);
FS_Error storage_file_get_error(File* f);
FS_Error storage_common_remove(Storage* s, const char* path);
