#define SF_ADAPTER_ONLY
#include "store_test.c"
#include "sf_browser.h"
static void touch(const char* p) { FILE* f=fopen(p,"wb");assert(f);fclose(f); }
int main(int argc,char** argv) {
 assert(argc==2 && chdir(argv[1])==0);
 Storage storage={0};SfBrowser b;sf_browser_init(&b,&storage);
 char root[SF_PATH],child[SF_PATH],path[SF_PATH];assert(getcwd(root,sizeof(root)));
 assert(sf_join(child,SF_PATH,root,"child"));assert(mkdir(child,0700)==0);
 assert(sf_join(path,SF_PATH,root,"One.nfc"));touch(path);
 uint32_t selected;SfRecord r;
 assert(sf_browser_open(&b,root,false,&selected));assert(sf_browser_count(&b)==2);
 sf_browser_remember(&b,1);assert(sf_browser_open(&b,child,false,&selected));
 unsigned reads=dir_opens,writes=file_writes;
 assert(sf_browser_open(&b,root,false,&selected) && selected==1);
 assert(dir_opens==reads && file_writes==writes && nfc_reads==0);
 SfStore meta;sf_store_init(&meta,&storage,"browser.a","browser.b",0x53464d45);
 memset(&r,0,sizeof(r));strcpy(r.path,path);r.flags=SF_FAVORITE;SF_SET_RATING(&r,4);
 assert(sf_store_update(&meta,&r,0));assert(sf_browser_status(&b,&meta));
 assert(sf_browser_get(&b,1,&r) && SF_RATING(&r)==4 && (r.flags & SF_FAVORITE));
 reads=file_opens;assert(sf_browser_status(&b,&meta));assert(file_opens==reads);
 r.flags=0;assert(sf_store_update(&meta,&r,0));assert(sf_browser_status(&b,&meta));
 assert(sf_browser_get(&b,1,&r) && r.flags==0);
 puts("PASS: cached Back restores selection without directory reads or writes; metadata cache follows generations");
 for(unsigned folder=0;folder<5;folder++) {
  char name[32];snprintf(name,sizeof(name),"folder%u",folder);assert(sf_join(child,SF_PATH,root,name));assert(mkdir(child,0700)==0);
  for(unsigned i=0;i<150;i++) {char name[220];memset(name,'x',sizeof(name));snprintf(name+190,30,"%u.nfc",i);assert(sf_join(path,SF_PATH,child,name));touch(path);}
  assert(sf_browser_open(&b,child,false,&selected));assert(sf_browser_count(&b)==150 && b.dirs[b.active].streaming);
  assert(sf_browser_get(&b,149,&r));assert(storage_common_stat(&storage,r.path,NULL)==FSE_OK);
  unsigned total=0;for(unsigned j=0;j<SF_BROWSER_SLOTS;j++)total+=b.dirs[j].capacity;
  assert(total<=SF_BROWSER_BUDGET);
 }
 sf_browser_clear(&b);
 puts("PASS: oversized folders retain all entries through streaming; cache stays within 24 KiB");
 SfStore index;sf_store_init(&index,&storage,"paths.a","paths.b",0x53465345);
 SfWriter w;sf_writer_begin(&w,&index);memset(&r,0,sizeof(r));strcpy(r.path,"/new/root");r.flags=SF_DIRECTORY;assert(sf_writer_add(&w,&r));
 r.flags=0;strcpy(r.path,"/new/root/One.nfc");assert(sf_writer_add(&w,&r));strcpy(r.path,"/new/root/copy/One.nfc");assert(sf_writer_add(&w,&r));assert(sf_writer_finish(&w));
 strcpy(r.path,"/old/One.nfc");r.plays=42;r.last=123;r.order=9;r.flags=SF_FAVORITE;SF_SET_RATING(&r,5);assert(sf_store_update(&meta,&r,0));
 strcpy(r.path,"/old/Unmatched.nfc");assert(sf_store_update(&meta,&r,0));
 sync_fail=true;assert(!sf_store_rebase(&meta,&index));sync_fail=false;
 assert(sf_store_find(&meta,"One.nfc",&r) && !strcmp(r.path,"/old/One.nfc"));
 assert(sf_store_rebase(&meta,&index));assert(meta.header.count==2);
 assert(sf_store_find(&meta,"One.nfc",&r) && !strcmp(r.path,"/new/root/One.nfc") && r.plays==42 && r.last==123 && r.order==9 && SF_RATING(&r)==5 && (r.flags&SF_FAVORITE));
 assert(sf_store_find(&meta,"Unmatched.nfc",&r) && !strcmp(r.path,"/old/Unmatched.nfc"));
 puts("PASS: root remapping preserves stats, handles duplicates and unmatched records, and survives failed commit");
 return 0;
}
