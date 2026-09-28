"""Compile unchanged production function bodies against the POSIX storage adapter."""
from pathlib import Path
import re
source = Path('storyflip/storyflip.c')
if not source.exists(): source = Path('storyflip.c')
src = source.read_text()
def function(name):
    m = re.search(r'static [^\n;]+\b' + name + r'\([^;]*?\) \{', src)
    assert m, name
    start = m.start(); end = m.end(); depth = 1
    # Functions below contain no unmatched braces in string literals.
    while depth:
        depth += (src[end] == '{') - (src[end] == '}'); end += 1
    return src[start:end]
enums = re.search(r'typedef enum \{.*?\} Action;', src, re.S).group()
app = re.search(r'typedef struct \{\n    Gui\*.*?\} App;', src, re.S).group()
rank = re.search(r'typedef struct \{ uint32_t offset.*?\} Rank;', src).group()
head = '''#define _GNU_SOURCE
#define SF_ADAPTER_ONLY
#include "store_test.c"
#include "sf_browser.h"
#include "sf_i18n.h"
#define T(key) sf_tr(a->language, key)
#include <strings.h>
#include <limits.h>
#define APP_DATA_PATH(x) "cache/" x
#define STORAGE_EXT_PATH_PREFIX "/ext"
#define SF_ROOT "/ext/nfc/StoryFlip"
#define SF_LIST APP_DATA_PATH("list.tmp")
#define SF_QUEUE APP_DATA_PATH("queue.tmp")
#define SF_ROWS 4
#define SF_VISIBLE 4
typedef void Gui; typedef void DialogsApp; typedef void ViewDispatcher; typedef void View;
typedef void TextInput; typedef void Nfc; typedef void NfcDevice; typedef void NfcListener;
'''
funcs = ['show_message','path_exists','target_meta','listener_event','emulation_stop','emulate','list_item','list_put','browse_list','index_list','build_list','search_matches_root','scan_start','scan_stop','scan_tick','confirm','save_config','use_root','choose_root','load_config','back','open_list','save_target','execute_action','home_open','ok','input','tick']
head += r"""
#define UNUSED(x) ((void)(x))
typedef int NfcCommand; typedef int NfcGenericEvent;
#define NfcCommandContinue 0
#define FuriHalNfcErrorNone 0
#define NfcProtocolSlix 1
static void view_dispatcher_stop(ViewDispatcher* p) { (void)p; }
static unsigned mock_heap = 100000, starts, stops;
static bool mock_load = true;
static int mock_hal, mock_protocol = NfcProtocolSlix;
static size_t memmgr_get_free_heap(void) { return mock_heap; }
static int furi_hal_nfc_is_hal_ready(void) { return mock_hal; }
static NfcDevice* nfc_device_alloc(void) { return malloc(1); }
static void nfc_device_free(NfcDevice* p) { free(p); }
static bool nfc_device_load(NfcDevice* p, const char* path) { (void)p; (void)path; return mock_load; }
static int nfc_device_get_protocol(NfcDevice* p) { (void)p; return mock_protocol; }
static const void* nfc_device_get_data(NfcDevice* p, int type) { (void)type; return p; }
static Nfc* nfc_alloc(void) { return malloc(1); }
static void nfc_free(Nfc* p) { free(p); }
static NfcListener* nfc_listener_alloc(Nfc* n, int type, const void* data) { (void)n; (void)type; (void)data; return malloc(1); }
static void nfc_listener_start(NfcListener* n, NfcCommand (*cb)(NfcGenericEvent, void*), void* ctx) { (void)n; (void)cb; (void)ctx; starts++; }
static void nfc_listener_stop(NfcListener* n) { (void)n; stops++; }
static void nfc_listener_free(NfcListener* n) { free(n); }
static uint32_t furi_hal_rtc_get_timestamp(void) { return 1234567; }
"""
model = re.search(r'typedef struct \{\n    Screen screen;.*?\} Model;', src, re.S).group()
body = head+enums+'\n'+model+'\n'+app+'\n'+rank+'''\nstatic void scan_stop(App*, bool);
static bool ui_ticks = true;
static unsigned refreshes, commits;
static Model display;
enum {InputTypeShort, InputTypeLong, InputTypeRepeat};
enum {InputKeyUp, InputKeyDown, InputKeyLeft, InputKeyRight, InputKeyOk, InputKeyBack};
typedef struct {int type,key;} InputEvent;
static Model* view_get_model(View* v) { (void)v;return &display; }
static void view_commit_model(View* v,bool update) { (void)v;(void)update;commits++; }
static void refresh(App* a) { (void)a;refreshes++; }
static void initialize(App* a) { a->initialized=true; }

static void set_ui_tick(App* a, bool enabled) { (void)a; ui_ticks=enabled; }
static void query_open(App* a) { a->screen = Home; }
'''+ '\n'.join(function(n) for n in funcs)
body += r'''
int main(int argc, char** argv) {
    assert(argc == 3);
    Storage storage = {0}; App a = {0}; a.storage = &storage; sf_browser_init(&a.browser, &storage);
    assert(chdir(argv[1]) == 0); mkdir("cache", 0700);
    sf_store_init(&a.search, &storage, "cache/search.a", "cache/search.b", 0x53465345);
    sf_store_init(&a.meta, &storage, "cache/meta.a", "cache/meta.b", 0x53464d45);
    assert(sf_copy(a.root, SF_PATH, argv[2])); assert(sf_copy(a.directory, SF_PATH, a.root));
    a.kind = Browse; assert(build_list(&a));
    for(unsigned i = 0; i < a.total; i++) { SfRecord r; assert(list_item(&a, i, &r)); assert(!strchr(r.path + strlen(a.root) + 1, '/')); }
    assert(nfc_reads == 0);
    printf("PASS: browse reads only %u immediate root entries, no NFC content\n", a.total);
    a.return_screen = Home; scan_start(&a);
    unsigned ticks = 0;
    while(a.screen == Scan) { scan_tick(&a); assert(++ticks < 10000); }
    unsigned file_count = a.scan_count;
    assert(file_count > 0);
    assert(a.search_valid); assert(a.search.header.count == file_count + 1); assert(nfc_reads == 0);
    printf("PASS: iterative search found %u NFC files in %u batches, no NFC content reads\n", file_count, ticks);
    a.kind = Search; a.query[0] = 0; a.selected = 0; assert(build_list(&a)); assert(a.total == file_count);
    for(unsigned i = 0; i < a.total; i++) { SfRecord r; assert(list_item(&a, i, &r)); struct stat st; assert(stat(r.path, &st) == 0); }
    puts("PASS: all search paths resolve, including long filenames");
    SfRecord sample; assert(list_item(&a, 0, &sample));
    const char* sample_name = sf_name(sample.path);
    size_t query_len = strlen(sample_name); if(query_len >= sizeof(a.query)) query_len = sizeof(a.query) - 1;
    memcpy(a.query, sample_name, query_len); a.query[query_len] = 0;
    assert(build_list(&a)); assert(a.total > 0 && a.total <= file_count);
    for(unsigned i = 0; i < a.total; i++) { SfRecord item; assert(list_item(&a, i, &item)); assert(strcasestr(sf_name(item.path), a.query)); }
    File* src = sf_store_open(&a.search); SfRecord r; assert(sf_record_read(src, &r));
    SfWriter w; sf_writer_begin(&w, &a.meta);
    for(unsigned i = 0; i < file_count; i++) {
        assert(sf_record_read(src, &r)); r.plays = i + 1; r.order = i + 1; r.last = 100;
        r.flags = i % 7 == 0 ? SF_FAVORITE : 0; SF_SET_RATING(&r, i % 6);
        assert(sf_writer_add(&w, &r));
    }
    sf_close(src); assert(sf_writer_finish(&w));
    unsigned top_count = file_count < 50 ? file_count : 50;
    a.kind = Top; assert(build_list(&a)); assert(a.total == top_count);
    for(unsigned i = 0; i < top_count; i++) { assert(list_item(&a, i, &r)); assert(r.plays == file_count - i); }
    unsigned recent_count = file_count < 5 ? file_count : 5;
    a.kind = Recent; assert(build_list(&a)); assert(a.total == recent_count);
    for(unsigned i = 0; i < recent_count; i++) { assert(list_item(&a, i, &r)); assert(r.order == file_count - i); }
    puts("PASS: Top and recent lists are correctly sorted, including equal timestamps");
    a.kind = Favorites; assert(build_list(&a)); assert(a.total == (file_count + 6) / 7);
    a.kind = Rated; a.rating = 5; assert(build_list(&a)); assert(a.total == file_count / 6);
    puts("PASS: favorites and rating filters use metadata only");
    unsigned generation = a.search.header.generation;
    a.return_screen = Home; scan_start(&a); scan_tick(&a); scan_stop(&a, false);
    assert(a.search.header.generation == generation && a.search_valid);
    puts("PASS: cancelled refresh preserves complete previous search index");
    a.kind = Top; a.selected = 0; assert(build_list(&a)); assert(list_item(&a, 0, &a.target));
    SfRecord before = a.target; a.order = file_count;
    mock_load = false; emulate(&a); assert(starts == 0 && a.screen == Message);
    assert(sf_store_find(&a.meta, sf_name(before.path), &r) && r.plays == before.plays);
    mock_load = true; mock_protocol = 99; emulate(&a); assert(starts == 0);
    mock_protocol = NfcProtocolSlix; mock_hal = 1; emulate(&a); assert(starts == 0);
    mock_hal = 0; mock_heap = 100; emulate(&a); assert(starts == 0);
    mock_heap = 100000; emulate(&a); assert(starts == 1 && a.screen == Emulate && !ui_ticks);
    assert(sf_store_find(&a.meta, sf_name(before.path), &r) && r.plays == before.plays + 1 && r.order == file_count + 1);
    a.initialized=true;
    unsigned ui_refreshes=refreshes,ui_commits=commits;
    for(unsigned n=0;n<100;n++)tick(&a);
    for(int key=InputKeyUp;key<=InputKeyOk;key++) {
        InputEvent e={InputTypeShort,key};assert(input(&e,&a));
    }
    assert(a.screen==Emulate && refreshes==ui_refreshes && commits==ui_commits);
    InputEvent stop={InputTypeShort,InputKeyBack};input(&stop,&a);
    assert(a.screen==List && ui_ticks && refreshes==ui_refreshes+1);
    puts("PASS: emulation disables ticks, ignores all keys except Back, and resumes UI after Back");
    assert(stops == 1 && !a.listener && !a.nfc && !a.device && ui_ticks);
    puts("PASS: load/protocol/hardware/RAM failures do not count; successful start counts once");

    load_config(&a); assert(a.language == 0 && !strcmp(a.root, SF_ROOT));
    for(unsigned lang=0; lang<3; lang++) {
        assert(save_config(&a, argv[2], lang)); a.language=99; strcpy(a.root,"/wrong");
        load_config(&a); assert(a.language==lang && !strcmp(a.root,argv[2]));
    }
    SfRecord legacy={0};strcpy(legacy.path,argv[2]);
    SfWriter config_writer;sf_writer_begin(&config_writer,&a.config);assert(sf_writer_add(&config_writer,&legacy));assert(sf_writer_finish(&config_writer));
    load_config(&a);assert(a.language==0);
    sync_fail=true;assert(!save_config(&a,"/wrong",2));sync_fail=false;
    load_config(&a);assert(a.language==0 && !strcmp(a.root,argv[2]));
    puts("PASS: all three languages and root survive reload; old config defaults to English; failed save preserves config");
    strcpy(a.directory,argv[2]);strcpy(a.root,"/old/root");a.picking_root=true;
    use_root(&a);assert(!a.picking_root && a.screen==Confirm && a.action==MakeSearch && a.return_screen==Settings);
    assert(!strcmp(a.root,argv[2]));
    a.picking_root=true;strcpy(a.directory,STORAGE_EXT_PATH_PREFIX);a.screen=List;
    back(&a);assert(a.screen==Settings && !a.picking_root);
    choose_root(&a); /* Host has no /ext: the picker reports an unreadable folder. */
    assert(a.screen==Message && !a.picking_root);
    puts("PASS: root selection persists and offers rebuild; Back at SD root cancels picker");
    a.screen=Settings;a.selected=0;ok(&a);assert(a.screen==Language);
    back(&a);assert(a.screen==Settings && a.selected==0);
    const unsigned settings[]={2,3,4,5,6};
    const Action actions[]={MakeSearch,ClearRecent,ClearRatings,ClearFavorites,CleanIndex};
    for(unsigned i=0;i<5;i++) {a.screen=Settings;a.selected=settings[i];ok(&a);assert(a.screen==Confirm && a.action==actions[i]);back(&a);}
    a.screen=Settings;a.selected=1;ok(&a);assert(a.screen==Message);
    a.screen=Settings;a.selected=7;ok(&a);assert(a.screen==Message);
    a.screen=Settings;a.selected=8;ok(&a);assert(a.screen==About);back(&a);assert(a.screen==Settings && a.selected==8);
    a.screen=Home;ui_refreshes=refreshes;ui_commits=commits;for(unsigned n=0;n<100;n++)tick(&a);
    assert(refreshes==ui_refreshes && commits==ui_commits);
    puts("PASS: new settings positions invoke correct actions; About is last; Home has no periodic redraws");
    sf_browser_clear(&a.browser);
    return 0;
}
''' 
Path('tests/core_test.generated.c').write_text(body)
