#include "sf_store.h"
#include "sf_browser.h"
#include "sf_i18n.h"
#include "sf_fonts.h"
#include "storyflip_icons.h"
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <gui/elements.h>
#include <dialogs/dialogs.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>
#include <furi_hal_nfc.h>
#include <furi_hal_rtc.h>
#include <datetime/datetime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>

#define SF_LIST APP_DATA_PATH("list.tmp")
#define SF_QUEUE APP_DATA_PATH("queue.tmp")
#define SF_ROOT EXT_PATH("nfc/StoryFlip")
#define SF_ROWS 4
#define SF_VISIBLE 4

typedef enum {
    Home, List, Stats, Ratings, Settings, Context, Rate, Info, Confirm,
    Message, Emulate, Scan, Splash, Language, About
} Screen;
typedef enum { Browse, Favorites, Recent, Top, Rated, Search } ListKind;
typedef enum { DeleteFile = 10, MakeSearch, ClearFavorites, ClearRatings, ClearRecent, CleanIndex } Action;
typedef struct {
    Screen screen;
    uint32_t selection, total, tick;
    uint8_t home, rows, language;
    char title[40];
    char lines[SF_ROWS][SF_NAME];
    char footer[40];
    uint32_t flags[SF_ROWS];
} Model;
typedef struct {
    Gui* gui; Storage* storage; DialogsApp* dialogs;
    ViewDispatcher* vd; View* view; TextInput* text;
    SfStore meta, search, config;
    SfBrowser browser;
    Screen screen, return_screen;
    ListKind kind;
    uint32_t selected, total, page, tick, order, list_selected;
    uint8_t home, rating, language;
    bool picking_root, browser_status_ready;
    bool initialized, search_valid, scan_for_search;
    char root[SF_PATH], directory[SF_PATH], query[64];
    SfRecord target;
    char message[SF_ROWS][SF_NAME];
    Action action;
    Nfc* nfc; NfcDevice* device; NfcListener* listener;
    SfWriter writer;
    File* queue; File* scan_dir;
    uint32_t queue_read, queue_count, scan_count;
    char scan_path[SF_PATH];
    bool scan_open;
} App;
static const char* home_names[] = {"Sammlung", "Zuletzt gehoert", "Favoriten", "Statistik", "Suche", "Einstellungen"};
static const char* settings_names[] = {"Language", "Index / Diagnose", "Suchindex erneuern", "Verlauf loeschen", "Bewertungen entfernen", "Favoriten entfernen", "Index bereinigen", "Root-Verzeichnis", "Ueber StoryFlip"};
static const char* stats_names[] = {"Bewertungen", "Meistgespielt"};
static const char* stars[] = {"5 Sterne", "4 Sterne", "3 Sterne", "2 Sterne", "1 Stern"};
#define T(key) sf_tr(a->language, key)
static void refresh(App* a);
static void tick(void* ctx);
static void set_ui_tick(App* a, bool enabled) {
    furi_event_loop_tick_set(view_dispatcher_get_event_loop(a->vd), enabled ? 150 : FuriWaitForever, enabled ? tick : NULL, a);
}
static bool save_config(App* a, const char* root, uint8_t language);
static bool build_list(App* a);
static void query_open(App* a);
static void scan_stop(App* a, bool finish);

static void show_message(App* a, Screen back, const char* l1, const char* l2, const char* l3) {
    memset(a->message, 0, sizeof(a->message));
    sf_copy(a->message[0], SF_NAME, l1);
    sf_copy(a->message[1], SF_NAME, l2 ? l2 : "");
    sf_copy(a->message[2], SF_NAME, l3 ? l3 : "");
    a->return_screen = back; a->screen = Message;
}
static bool path_exists(App* a, const char* path) {
    FileInfo info;
    return storage_common_stat(a->storage, path, &info) == FSE_OK && !file_info_is_dir(&info);
}
static bool list_item(App* a, uint32_t index, SfRecord* r) {
    if(index >= a->total) return false;
    if(a->kind == Browse) {
        if(a->picking_root && index == 0) {
            memset(r, 0, sizeof(*r)); sf_copy(r->path, SF_PATH, a->directory); r->flags = SF_DIRECTORY | 512u; return true;
        }
        return sf_browser_get(&a->browser, index - (a->picking_root ? 1 : 0), r);
    }
    File* f = storage_file_alloc(a->storage);
    bool ok = storage_file_open(f, SF_LIST, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_seek(f, index * sizeof(*r), true) && sf_record_read(f, r);
    sf_close(f); return ok;
}
static bool list_put(App* a, File* out, const SfRecord* r) {
    if(a->total >= UINT32_MAX / sizeof(*r) || !sf_record_write(out, r)) return false;
    a->total++; return true;
}
static bool browse_list(App* a, File* out) {
    UNUSED(out);
    uint32_t saved = 0;
    bool ok = sf_browser_open(&a->browser, a->directory, a->picking_root, &saved);
    if(ok) { a->total = sf_browser_count(&a->browser) + (a->picking_root ? 1 : 0); a->selected = saved; }
    return ok;
}
typedef struct { uint32_t offset, score, order; } Rank;
static bool index_list(App* a, File* out) {
    SfStore* s = a->kind == Search ? &a->search : &a->meta;
    File* f = sf_store_open(s);
    if(s->active < 0) return true;
    if(!f) return false;
    SfRecord r;
    Rank best[50]; size_t count = 0;
    bool ranked = a->kind == Recent || a->kind == Top;
    size_t limit = a->kind == Recent ? 5 : 50;
    bool ok = true;
    for(uint32_t i = 0; ok && i < s->header.count; i++) {
        if(!sf_record_read(f, &r)) { ok = false; break; }
        bool take = false;
        if(a->kind == Favorites) take = r.flags & SF_FAVORITE;
        if(a->kind == Rated) take = SF_RATING(&r) == a->rating;
        if(a->kind == Search) take = !(r.flags & SF_DIRECTORY) && strcasestr(sf_name(r.path), a->query);
        if(ranked) {
            uint32_t score = a->kind == Recent ? r.order : r.plays;
            if(!score) continue;
            size_t j = 0;
            while(j < count && (best[j].score > score ||
                  (best[j].score == score && best[j].order >= r.order))) j++;
            if(j < limit) {
                if(count < limit) count++;
                for(size_t k = count - 1; k > j; k--) best[k] = best[k - 1];
                best[j] = (Rank){i, score, r.order};
            }
        } else if(take) ok = list_put(a, out, &r);
    }
    for(size_t i = 0; ok && ranked && i < count; i++) {
        ok = storage_file_seek(f, sizeof(SfHeader) + best[i].offset * sizeof(r), true) &&
             sf_record_read(f, &r) && list_put(a, out, &r);
    }
    sf_close(f); return ok;
}
static bool build_list(App* a) {
    a->total = 0; a->tick = 0;
    bool ok;
    if(a->kind == Browse) { ok = browse_list(a, NULL); a->browser_status_ready = ok && sf_browser_status(&a->browser, &a->meta); }
    else {
        File* out = storage_file_alloc(a->storage);
        ok = storage_file_open(out, SF_LIST, FSAM_WRITE, FSOM_CREATE_ALWAYS);
        if(ok) ok = index_list(a, out);
        if(ok) ok = storage_file_sync(out);
        sf_close(out);
    }
    if(a->selected >= a->total) a->selected = a->total ? a->total - 1 : 0;
    a->screen = List;
    if(!ok) {
        a->total = 0;
        show_message(a, a->picking_root ? Settings : Home, T("Liste nicht lesbar"), T("SD/Pfad pruefen."), T("Max. Pfad: 1023 Byte"));
        a->picking_root = false;
    }
    return ok;
}
static void open_list(App* a, ListKind kind) {
    a->picking_root = false; a->kind = kind; a->selected = 0;
    if(kind == Browse) sf_copy(a->directory, SF_PATH, a->root);
    build_list(a);
}
static void target_meta(App* a) {
    SfRecord r;
    if(sf_store_find(&a->meta, sf_name(a->target.path), &r)) {
        // Preserve the selected path while sharing metadata by filename.
        sf_copy(r.path, sizeof(r.path), a->target.path); a->target = r;
    }
}
static void save_target(App* a) {
    if(!sf_store_update(&a->meta, &a->target, 0))
        show_message(a, Context, T("Speichern fehlgeschlagen"), T("SD-Karte pruefen."), T("Alter Index bleibt."));
}
static NfcCommand listener_event(NfcGenericEvent event, void* context) {
    UNUSED(event); UNUSED(context); return NfcCommandContinue;
}
static void emulation_stop(App* a) {
    if(a->screen == Emulate) set_ui_tick(a, true);
    if(a->listener) { nfc_listener_stop(a->listener); nfc_listener_free(a->listener); a->listener = NULL; }
    if(a->device) { nfc_device_free(a->device); a->device = NULL; }
    if(a->nfc) { nfc_free(a->nfc); a->nfc = NULL; }
}
static void emulate(App* a) {
    if(!path_exists(a, a->target.path)) {
        show_message(a, List, T("Datei fehlt"), T("Pfad nicht mehr gueltig."), T("In der Sammlung suchen.")); return;
    }
    if(memmgr_get_free_heap() < 40000) sf_browser_clear(&a->browser);
    if(memmgr_get_free_heap() < 40000 || furi_hal_nfc_is_hal_ready() != FuriHalNfcErrorNone) {
        show_message(a, List, T("NFC nicht bereit"), T("NFC belegt oder"), T("zu wenig freier RAM.")); return;
    }
    a->device = nfc_device_alloc();
    if(!nfc_device_load(a->device, a->target.path)) {
        emulation_stop(a); show_message(a, List, T("NFC-Datei ungueltig"), T("Laden fehlgeschlagen."), T("Kein Start gezaehlt.")); return;
    }
    if(nfc_device_get_protocol(a->device) != NfcProtocolSlix) {
        emulation_stop(a); show_message(a, List, T("Protokoll nicht SLIX"), T("Diese Version emuliert"), T("SLIX-Dateien.")); return;
    }
    a->nfc = nfc_alloc();
    a->listener = nfc_listener_alloc(a->nfc, NfcProtocolSlix, nfc_device_get_data(a->device, NfcProtocolSlix));
    nfc_listener_start(a->listener, listener_event, a);
    // The SDK has a void start API. Returning from it is the start boundary.
    // The listener owns its own copy. Never save its potentially mutated data.
    nfc_device_free(a->device); a->device = NULL;
    a->screen = Emulate;
    set_ui_tick(a, false);
    target_meta(a);
    if(a->target.plays < UINT32_MAX) a->target.plays++;
    a->target.last = furi_hal_rtc_get_timestamp();
    if(a->order < UINT32_MAX) a->order++;
    a->target.order = a->order;
    memset(a->message, 0, sizeof(a->message));
    if(!sf_store_update(&a->meta, &a->target, 0))
        sf_copy(a->message[0], SF_NAME, T("Statistik nicht gespeichert"));
}
static void confirm(App* a, Action action, Screen back) {
    a->action = action; a->return_screen = back; a->screen = Confirm; a->tick = 0;
}
static bool search_matches_root(App* a) {
    SfRecord r;
    File* f = sf_store_open(&a->search);
    bool ok = sf_record_read(f, &r) && (r.flags & SF_DIRECTORY) && !strcmp(r.path, a->root);
    sf_close(f); return ok;
}
static void scan_start(App* a) {
    sf_browser_clear(&a->browser);
    a->scan_for_search = a->return_screen == Home;
    a->queue = storage_file_alloc(a->storage);
    a->scan_dir = storage_file_alloc(a->storage);
    a->scan_open = false;
    a->queue_count = 1; a->queue_read = 0; a->scan_count = 0;
    sf_writer_begin(&a->writer, &a->search);
    SfRecord root = {0}; sf_copy(root.path, SF_PATH, a->root); root.flags = SF_DIRECTORY;
    bool ok = storage_file_open(a->queue, SF_QUEUE, FSAM_READ_WRITE, FSOM_CREATE_ALWAYS) &&
              sf_record_write(a->queue, &root) && sf_writer_add(&a->writer, &root);
    a->screen = Scan;
    if(!ok) scan_stop(a, false);
}
static void scan_stop(App* a, bool finish) {
    if(a->scan_open) storage_dir_close(a->scan_dir);
    a->scan_open = false;
    if(a->scan_dir) storage_file_free(a->scan_dir);
    a->scan_dir = NULL;
    sf_close(a->queue); a->queue = NULL;
    bool ok = false;
    if(finish) ok = sf_writer_finish(&a->writer);
    else sf_writer_abort(&a->writer);
    storage_common_remove(a->storage, SF_QUEUE);
    a->search_valid = search_matches_root(a);
    if(ok && !sf_store_rebase(&a->meta, &a->search)) {
        show_message(a, Settings, T("Search saved; paths unchanged"), T("Speichern fehlgeschlagen"), T("SD-Karte pruefen."));
    } else if(ok && a->scan_for_search) query_open(a);
    else if(ok) show_message(a, Settings, T("Suchindex gespeichert"), T("Suche ist bereit."), "");
    else show_message(a, a->scan_for_search ? Home : Settings, T("Suchindex nicht ersetzt"), T("Abbruch oder SD/Pfadfehler"), T("Alter Stand bleibt erhalten."));
}
static void scan_tick(App* a) {
    // Iterative breadth-first scan. Pending directories live on SD, not the C stack.
    for(unsigned batch = 0; batch < 12 && a->screen == Scan; batch++) {
        SfRecord r = {0};
        if(!a->scan_open) {
            if(a->queue_read >= a->queue_count) { scan_stop(a, true); return; }
            if(!storage_file_seek(a->queue, a->queue_read * sizeof(r), true) || !sf_record_read(a->queue, &r)) {
                scan_stop(a, false); return;
            }
            a->queue_read++;
            sf_copy(a->scan_path, SF_PATH, r.path);
            a->scan_open = storage_dir_open(a->scan_dir, r.path);
            if(!a->scan_open) { scan_stop(a, false); return; }
        }
        FileInfo fi; char name[SF_NAME];
        if(!storage_dir_read(a->scan_dir, &fi, name, sizeof(name))) {
            FS_Error error = storage_file_get_error(a->scan_dir);
            storage_dir_close(a->scan_dir); a->scan_open = false;
            if(error != FSE_OK && error != FSE_NOT_EXIST) { scan_stop(a, false); return; }
            continue;
        }
        if(!strcmp(name, ".") || !strcmp(name, "..")) continue;
        bool dir = file_info_is_dir(&fi);
        if(!dir && !sf_is_nfc(name)) continue;
        memset(&r, 0, sizeof(r));
        if(!sf_join(r.path, SF_PATH, a->scan_path, name)) { scan_stop(a, false); return; }
        if(dir) {
            r.flags = SF_DIRECTORY;
            if(a->queue_count >= UINT32_MAX / sizeof(r) ||
               !storage_file_seek(a->queue, a->queue_count * sizeof(r), true) || !sf_record_write(a->queue, &r)) {
                scan_stop(a, false); return;
            }
            a->queue_count++;
        } else {
            if(!sf_writer_add(&a->writer, &r)) { scan_stop(a, false); return; }
            a->scan_count++;
        }
    }
}
static void query_done(void* ctx) { view_dispatcher_send_custom_event(((App*)ctx)->vd, 1); }
static void query_open(App* a) {
    a->screen = Home;
    text_input_reset(a->text);
    text_input_set_header_text(a->text, T("Dateiname suchen"));
    text_input_set_minimum_length(a->text, 0);
    text_input_set_result_callback(a->text, query_done, a, a->query, sizeof(a->query), false);
    view_dispatcher_switch_to_view(a->vd, 1);
}
static uint32_t query_back(void* ctx) { UNUSED(ctx); return 0; }
static void choose_root(App* a) {
    a->picking_root = true; a->kind = Browse;
    sf_copy(a->directory, SF_PATH, STORAGE_EXT_PATH_PREFIX); a->selected = 0;
    build_list(a);
}
static bool save_config(App* a, const char* root, uint8_t language) {
    SfRecord record = {0};
    if(!sf_copy(record.path, SF_PATH, root)) return false;
    record.plays = language + 1;
    SfWriter w; sf_writer_begin(&w, &a->config); sf_writer_add(&w, &record);
    if(!sf_writer_finish(&w)) return false;
    sf_copy(a->root, SF_PATH, root); a->language = language;
    return true;
}
static void use_root(App* a) {
    bool changed = strcmp(a->root, a->directory) != 0;
    FileInfo info;
    if(storage_common_stat(a->storage, a->directory, &info) != FSE_OK || !file_info_is_dir(&info) ||
       !save_config(a, a->directory, a->language)) {
        show_message(a, Settings, T("Root nicht gespeichert"), T("SD-Karte pruefen."), ""); return;
    }
    a->picking_root = false; a->screen = Settings; a->selected = 7;
    a->search_valid = search_matches_root(a);
    if(changed) confirm(a, MakeSearch, Settings);
}
static void execute_action(App* a) {
    if(a->action == MakeSearch) { scan_start(a); return; }
    if(a->action == DeleteFile) {
        if(storage_common_remove(a->storage, a->target.path) == FSE_OK) {
            char parent[SF_PATH]; sf_copy(parent, SF_PATH, a->target.path);
            char* slash = strrchr(parent, '/'); if(slash) *slash = 0;
            sf_browser_invalidate(&a->browser, parent);
            a->selected = a->list_selected; build_list(a);
        }
        else show_message(a, Context, T("Loeschen fehlgeschlagen"), T("SD/Pfad pruefen."), "");
        return;
    }
    int clear = a->action == ClearFavorites ? 1 : a->action == ClearRatings ? 2 : a->action == ClearRecent ? 3 : 4;
    if(sf_store_update(&a->meta, NULL, clear)) show_message(a, Settings, T("Erledigt"), T("Index gespeichert."), "");
    else show_message(a, Settings, T("Speichern fehlgeschlagen"), T("Alter Index bleibt."), T("SD-Karte pruefen."));
}

static void draw_text(Canvas* c, int x, int y, unsigned width, const char* text, uint32_t tick) {
    char buf[SF_NAME];
    size_t len = strlen(text), start = 0;
    if(canvas_string_width(c, text) > width && tick > 10) {
        start = ((tick - 10) / 3) % (len + 10);
        if(start > len) start = 0;
        while(start < len && (((uint8_t)text[start] & 0xc0) == 0x80)) start++;
    }
    sf_copy(buf, sizeof(buf), text + start);
    size_t n = strlen(buf);
    while(n && canvas_string_width(c, buf) > width) {
        do { n--; } while(n && (((uint8_t)buf[n] & 0xc0) == 0x80));
        buf[n] = 0;
    }
    canvas_draw_str(c, x, y, buf);
}
static void draw(Canvas* c, void* ctx) {
    Model* m = ctx;
    const Model* a = m; UNUSED(a);
    canvas_clear(c); canvas_set_color(c, ColorBlack); canvas_set_bitmap_mode(c, true);
    if(m->screen == Home) {
        const Icon* icons[] = {&I_collection, &I_recent, &I_favorites, &I_statistics, &I_search, &I_settings};
        for(unsigned i = 0; i < 6; i++) {
            int x = (i % 3) * 43 + 1, y = (i / 3) * 25 + 1;
            canvas_draw_rbox(c, x + 2, y + 2, 38, 22, 2);
            canvas_set_color(c, ColorWhite); canvas_draw_rbox(c, x, y, 38, 22, 2);
            canvas_set_color(c, ColorBlack); canvas_draw_rframe(c, x, y, 38, 22, 2);
            if(i == m->home) {
                canvas_draw_rbox(c, x + 1, y + 1, 36, 20, 2); canvas_set_color(c, ColorWhite);
                /* Three white pixels per corner, two equal legs sharing their vertex. */
                canvas_draw_line(c, x + 1, y + 1, x + 2, y + 1);
                canvas_draw_line(c, x + 1, y + 1, x + 1, y + 2);
                canvas_draw_line(c, x + 35, y + 1, x + 36, y + 1);
                canvas_draw_line(c, x + 36, y + 1, x + 36, y + 2);
                canvas_draw_line(c, x + 1, y + 20, x + 2, y + 20);
                canvas_draw_line(c, x + 1, y + 19, x + 1, y + 20);
                canvas_draw_line(c, x + 35, y + 20, x + 36, y + 20);
                canvas_draw_line(c, x + 36, y + 19, x + 36, y + 20);
            }
            canvas_draw_icon(c, x + 9, y + 1, icons[i]);
            canvas_set_color(c, ColorBlack);
        }
        const char* s = sf_tr(m->language, home_names[m->home]);
        char label[SF_NAME]; sf_copy(label, sizeof(label), s);
        canvas_set_custom_u8g2_font(c, sf_font_regular);
        size_t n = strlen(label);
        while(n && canvas_string_width(c, label) > 120) {
            do { n--; } while(n && (((uint8_t)label[n] & 0xc0) == 0x80));
            label[n] = 0;
        }
        canvas_draw_str_aligned(c, 64, 61, AlignCenter, AlignBottom, label);
        return;
    }
    if(m->screen == Splash) {
        const Icon* splashes[] = {&I_splash_EN, &I_splash_DE, &I_splash_FR};
        canvas_draw_icon(c, 0, 0, splashes[m->language < 3 ? m->language : 0]); return;
    }
    canvas_set_custom_u8g2_font(c, sf_font_bold);
    draw_text(c, 2, 10, (m->screen == List || m->screen == Settings) ? 92 : 124, m->title, 0);
    canvas_set_custom_u8g2_font(c, sf_font_regular);
    if(m->screen == List || m->screen == Settings) {
        char n[24]; snprintf(n, sizeof(n), "%lu/%lu", (unsigned long)(m->total ? m->selection + 1 : 0), (unsigned long)m->total);
        canvas_draw_str_aligned(c, 127, 10, AlignRight, AlignBottom, n);
    }
    canvas_draw_line(c, 0, 13, 127, 13);
    bool menu = m->screen == List || m->screen == Settings || m->screen == Context || m->screen == Stats || m->screen == Ratings || m->screen == Rate || m->screen == Language;
    for(unsigned i = 0; i < m->rows; i++) {
        int y = menu ? 23 + i * 12 : 23 + i * 11;
        int text_y = y + (menu ? 1 : 0);
        bool use_folder = m->screen == List && (m->flags[i] & 512u);
        canvas_set_custom_u8g2_font(c, use_folder ? sf_font_bold : sf_font_regular);
        bool selected = menu && i == m->selection % SF_VISIBLE;
        if(selected) { canvas_draw_rbox(c, 0, y - 9, 128, 12, 2); canvas_set_color(c, ColorWhite); }
        if(m->screen == List) {
            if(use_folder) canvas_draw_str(c, 2, text_y, ">");
            else canvas_draw_icon(c, 2, y - 8, (m->flags[i] & SF_DIRECTORY) ? &I_folder : &I_nfc_file);
            bool has_state = (m->flags[i] & (SF_FAVORITE | 0x70u)) != 0;
            int text_x = use_folder ? 10 : 15;
            draw_text(c, text_x, text_y, (has_state ? 108 : 126) - text_x, m->lines[i], selected ? m->tick : 0);
            char state[5] = "";
            uint32_t rating = (m->flags[i] >> 4) & 7u;
            snprintf(state, sizeof(state), "%c%c", m->flags[i] & SF_FAVORITE ? '*' : ' ', rating ? '0' + (char)rating : ' ');
            canvas_draw_str_aligned(c, 126, text_y, AlignRight, AlignBottom, state);
        } else draw_text(c, 2, text_y, 124, m->lines[i], m->screen == Emulate ? 0 : (selected || !menu ? m->tick : 0));
        canvas_set_color(c, ColorBlack);
    }
    if(m->footer[0]) canvas_draw_str_aligned(c, 64, 63, AlignCenter, AlignBottom, m->footer);
}
static void refresh(App* a) {
    // Build display state outside the view lock. Rendering never accesses storage.
    Model* m = malloc(sizeof(Model));
    memset(m, 0, sizeof(*m)); m->screen = a->screen; m->home = a->home; m->language = a->language;
    m->tick = a->tick; m->selection = a->selected; m->total = a->total;
    const char* const* labels = NULL; unsigned count = 0;
    switch(a->screen) {
    case Home: case Splash: break;
    case List: {
        const char* titles[] = {T("Sammlung"), T("Favoriten"), T("Zuletzt"), "Top 50", T("Bewertungen"), T("Suche")};
        sf_copy(m->title, sizeof(m->title), titles[a->kind]);
        a->page = a->selected / SF_VISIBLE * SF_VISIBLE;
        for(uint32_t i = a->page; i < a->total && m->rows < SF_VISIBLE; i++) {
            SfRecord r;
            if(!list_item(a, i, &r)) break;
            unsigned row = m->rows++;
            sf_copy(m->lines[row], SF_NAME, (r.flags & 512u) ? T("Use this folder") : sf_name(r.path));
            if(!(r.flags & SF_DIRECTORY)) {
                size_t n = strlen(m->lines[row]);
                if(n > 4) m->lines[row][n - 4] = 0;
                
            }
            m->flags[row] = r.flags;
        }
        if(a->picking_root) sf_copy(m->title, sizeof(m->title), T("Select folder"));
        else if(a->kind != Browse || !a->browser_status_ready) {
            // One sequential metadata pass for the whole page, not one per row.
            SfRecord meta; File* f = sf_store_open(&a->meta);
            while(sf_record_read(f, &meta)) {
                char display[SF_NAME]; sf_copy(display, SF_NAME, sf_name(meta.path));
                size_t n = strlen(display); if(n > 4) display[n - 4] = 0;
                for(unsigned row = 0; row < m->rows; row++) {
                    if(!(m->flags[row] & SF_DIRECTORY) && !strcmp(display, m->lines[row])) m->flags[row] = meta.flags;
                }
            }
            sf_close(f);
        }
        if(!a->total) { sf_copy(m->lines[0], SF_NAME, T("Keine Eintraege")); m->rows = 1; }
        break;
    }
    case Stats: sf_copy(m->title, sizeof(m->title), T("Statistik")); labels = stats_names; count = 2; break;
    case Ratings: case Rate: sf_copy(m->title, sizeof(m->title), T("Bewertung")); labels = stars; count = 5; break;
    case Settings: sf_copy(m->title, sizeof(m->title), T("Einstellungen")); labels = settings_names; count = 9; m->total = 9; break;
    case Language: { static const char* languages[] = {"English", "Deutsch", "Français"}; sf_copy(m->title, sizeof(m->title), T("Language")); labels = languages; count = 3; break; }
    case Context: {
        sf_copy(m->title, sizeof(m->title), T("Aktionen"));
        const char* actions[] = {T("Informationen"), a->target.flags & SF_FAVORITE ? T("Favorit entfernen") : T("Favorisieren"), T("Bewerten"), T("Bewertung entfernen"), T("NFC-Datei loeschen")};
        unsigned first = a->selected / SF_VISIBLE * SF_VISIBLE;
        for(unsigned i = first; i < 5 && m->rows < SF_VISIBLE; i++) sf_copy(m->lines[m->rows++], SF_NAME, actions[i]);
        break;
    }
    case Info:
        sf_copy(m->title, sizeof(m->title), T("Dateiinfo")); m->rows = 4;
        sf_copy(m->lines[0], SF_NAME, sf_name(a->target.path));
        snprintf(m->lines[1], SF_NAME, T("Favorit: %s  Sterne: %lu"), a->target.flags & SF_FAVORITE ? T("Ja") : T("Nein"), (unsigned long)SF_RATING(&a->target));
        snprintf(m->lines[2], SF_NAME, T("Starts: %lu"), (unsigned long)a->target.plays);
        if(a->target.last) {
            DateTime date; datetime_timestamp_to_datetime(a->target.last, &date);
            snprintf(m->lines[3], SF_NAME, "%02u.%02u.%04u %02u:%02u", date.day, date.month, date.year, date.hour, date.minute);
        } else sf_copy(m->lines[3], SF_NAME, T("Noch nicht gestartet"));
         break;
    case Confirm:
        sf_copy(m->title, sizeof(m->title), T("Bestaetigen")); m->rows = 3;
        if(a->action == MakeSearch) {
            sf_copy(m->lines[0], SF_NAME, T("Vollstaendigen Suchindex"));
            sf_copy(m->lines[1], SF_NAME, T("erstellen? Das kann dauern."));
            sf_copy(m->lines[2], SF_NAME, a->return_screen == Settings ? T("Current root will be indexed.") : T("Alle Unterordner lesen."));
            sf_copy(m->footer, sizeof(m->footer), T("OK: Starten   Back: Abbruch"));
        } else {
            const char* what = a->action == DeleteFile ? T("Diese NFC-Datei loeschen?") : a->action == ClearFavorites ? T("Alle Favoriten entfernen?") : a->action == ClearRatings ? T("Alle Bewertungen entfernen?") : a->action == ClearRecent ? T("Verlauf loeschen?") : T("Fehlende Indexpfade loeschen?");
            sf_copy(m->lines[0], SF_NAME, what);
            sf_copy(m->lines[1], SF_NAME, a->action == DeleteFile ? sf_name(a->target.path) : T("Aenderung bestaetigen."));
            sf_copy(m->lines[2], SF_NAME, a->action == CleanIndex ? T("Metadaten dazu entfallen.") : "");
            sf_copy(m->footer, sizeof(m->footer), T("OK: Ja   Back: Abbrechen"));
        } break;
    case About:
        sf_copy(m->title, sizeof(m->title), "StoryFlip"); m->rows = 2;
        sf_copy(m->lines[0], SF_NAME, "Version 0.4.2");
        sf_copy(m->lines[1], SF_NAME, "Vibecode Version");
        sf_copy(m->footer, sizeof(m->footer), T("OK / Back: Zurueck")); break;
    case Message:
        sf_copy(m->title, sizeof(m->title), "StoryFlip"); m->rows = a->message[3][0] ? 4 : 3;
        memcpy(m->lines, a->message, sizeof(m->lines));
        if(m->rows < 4) sf_copy(m->footer, sizeof(m->footer), T("OK / Back: Zurueck"));
        break;
    case Emulate:
        sf_copy(m->title, sizeof(m->title), T("SLIX Emulation")); m->rows = 3;
        sf_copy(m->lines[0], SF_NAME, sf_name(a->target.path));
        sf_copy(m->lines[1], SF_NAME, T("Emulation aktiv"));
        sf_copy(m->lines[2], SF_NAME, T("An Lesegerät halten."));
        if(a->message[0][0]) sf_copy(m->lines[2], SF_NAME, a->message[0]);
        sf_copy(m->footer, sizeof(m->footer), T("Back: Stoppen")); break;
    case Scan:
        sf_copy(m->title, sizeof(m->title), T("Suchindex")); m->rows = 3;
        snprintf(m->lines[0], SF_NAME, T("%lu NFC-Dateien gefunden"), (unsigned long)a->scan_count);
        snprintf(m->lines[1], SF_NAME, T("Ordner: %lu / %lu"), (unsigned long)a->queue_read, (unsigned long)a->queue_count);
        sf_copy(m->lines[2], SF_NAME, sf_name(a->scan_path));
        sf_copy(m->footer, sizeof(m->footer), T("Back: Abbrechen")); break;
    }
    if(labels) {
        unsigned first = a->selected / SF_VISIBLE * SF_VISIBLE;
        for(unsigned i = first; i < count && m->rows < SF_VISIBLE; i++) sf_copy(m->lines[m->rows++], SF_NAME, sf_tr(a->language, labels[i]));
    }
    Model* model = view_get_model(a->view); *model = *m;
    view_commit_model(a->view, true); free(m);
}
static void home_open(App* a) {
    a->selected = 0;
    switch(a->home) {
    case 0: open_list(a, Browse); break;
    case 1: open_list(a, Recent); break;
    case 2: open_list(a, Favorites); break;
    case 3: a->screen = Stats; break;
    case 4:
        if(a->search_valid) query_open(a);
        else confirm(a, MakeSearch, Home);
        break;
    case 5: a->screen = Settings; break;
    }
}
static void back(App* a) {
    switch(a->screen) {
    case Home: view_dispatcher_stop(a->vd); break;
    case List:
        if(a->picking_root && !strcmp(a->directory, STORAGE_EXT_PATH_PREFIX)) {
            a->picking_root = false; a->screen = Settings; a->selected = 7; break;
        }
        if(a->kind == Browse && strcmp(a->directory, a->picking_root ? STORAGE_EXT_PATH_PREFIX : a->root)) {
            char* slash = strrchr(a->directory, '/');
            if(slash) *slash = 0;
            if(!a->picking_root && strlen(a->directory) < strlen(a->root)) sf_copy(a->directory, SF_PATH, a->root);
            a->selected = 0; build_list(a);
        } else { a->screen = a->kind == Rated ? Ratings : a->kind == Top ? Stats : Home; a->selected = 0; }
        break;
    case Context: a->selected = a->list_selected; build_list(a); break;
    case Info: case Rate: a->screen = Context; a->selected = 0; break;
    case Confirm: a->screen = a->return_screen; break;
    case Message:
        a->screen = a->return_screen;
        if(a->screen == List) build_list(a);
        break;
    case Emulate: emulation_stop(a); build_list(a); break;
    case Scan: scan_stop(a, false); break;
    case About: a->screen = Settings; a->selected = 8; break;
    case Language: a->screen = Settings; a->selected = 0; break;
    case Ratings: a->screen = Stats; a->selected = 0; break;
    default: a->screen = Home; a->selected = 0; break;
    }
}
static void ok(App* a) {
    switch(a->screen) {
    case Home: home_open(a); break;
    case List:
        if(list_item(a, a->selected, &a->target)) {
            if(a->picking_root && (a->target.flags & 512u)) use_root(a);
            else if(a->target.flags & SF_DIRECTORY) {
                sf_browser_remember(&a->browser, a->selected);
                sf_copy(a->directory, SF_PATH, a->target.path); a->selected = 0; build_list(a);
            }
            else emulate(a);
        } break;
    case Stats:
        if(a->selected == 0) { a->screen = Ratings; a->selected = 0; }
        else open_list(a, Top);
        break;
    case Ratings: a->rating = 5 - a->selected; open_list(a, Rated); break;
    case Rate:
        SF_SET_RATING(&a->target, 5 - a->selected);
        a->screen = Context; a->selected = 2; save_target(a); break;
    case Context:
        if(a->selected == 0) a->screen = Info;
        else if(a->selected == 1) { a->target.flags ^= SF_FAVORITE; save_target(a); }
        else if(a->selected == 2) { a->screen = Rate; a->selected = 0; }
        else if(a->selected == 3) { SF_SET_RATING(&a->target, 0); save_target(a); }
        else confirm(a, DeleteFile, Context);
        break;
    case Language:
        if(save_config(a, a->root, a->selected)) { a->screen = Settings; a->selected = 0; }
        else show_message(a, Settings, T("Speichern fehlgeschlagen"), T("SD-Karte pruefen."), "");
        break;
    case Settings:
        switch(a->selected) {
        case 7: choose_root(a); break;
        case 2: confirm(a, MakeSearch, Settings); break;
        case 5: confirm(a, ClearFavorites, Settings); break;
        case 4: confirm(a, ClearRatings, Settings); break;
        case 3: confirm(a, ClearRecent, Settings); break;
        case 1:
            show_message(a, Settings, "", "", "");
            snprintf(a->message[0], SF_NAME, T("Metadaten: %lu"), (unsigned long)a->meta.header.count);
            snprintf(a->message[1], SF_NAME, T("Suchindex: %lu %s"), (unsigned long)(a->search.header.count ? a->search.header.count - 1 : 0), a->search_valid ? T("gueltig") : T("erneuern"));
            snprintf(a->message[2], SF_NAME, T("Freier RAM: %lu Byte"), (unsigned long)memmgr_get_free_heap());
            snprintf(a->message[3], SF_NAME, T("Root: %.240s"), a->root); break;
        case 6: confirm(a, CleanIndex, Settings); break;
        case 0: a->screen = Language; a->selected = a->language; break;
        case 8: a->screen = About; break;
        } break;
    case Confirm: execute_action(a); break;
    case About: case Message: back(a); break;
    default: break;
    }
}
static bool input(InputEvent* ev, void* ctx) {
    App* a = ctx;
    if(!a->initialized) return true;
    bool short_press = ev->type == InputTypeShort;
    bool navigation = short_press || ev->type == InputTypeRepeat;
    if(a->screen == Emulate) {
        if(short_press && ev->key == InputKeyBack) { back(a); refresh(a); }
        return true;
    }
    if(short_press && ev->key == InputKeyBack) back(a);
    else if(short_press && ev->key == InputKeyOk) ok(a);
    else if(a->screen == List && ev->type == InputTypeLong && ev->key == InputKeyRight) {
        if(list_item(a, a->selected, &a->target) && !(a->target.flags & SF_DIRECTORY)) {
            target_meta(a); a->list_selected = a->selected; a->selected = 0; a->screen = Context;
        }
    } else if(navigation && a->screen == Home) {
        if(ev->key == InputKeyLeft && a->home % 3) a->home--;
        if(ev->key == InputKeyRight && a->home % 3 < 2) a->home++;
        if(ev->key == InputKeyUp && a->home >= 3) a->home -= 3;
        if(ev->key == InputKeyDown && a->home < 3) a->home += 3;
    } else if(navigation) {
        uint32_t count = a->screen == List ? a->total : a->screen == Settings ? 9 : a->screen == Language ? 3 : a->screen == Stats ? 2 :
                         (a->screen == Ratings || a->screen == Rate || a->screen == Context) ? 5 : 0;
        if(ev->key == InputKeyUp && a->selected && count) a->selected--;
        if(ev->key == InputKeyDown && a->selected + 1 < count) a->selected++;
    } else return true;
    if(a->screen == List && a->kind == Browse) sf_browser_remember(&a->browser, a->selected);
    a->tick = 0;
    if(navigation && (ev->key == InputKeyUp || ev->key == InputKeyDown) && a->screen == List && a->page == a->selected / SF_VISIBLE * SF_VISIBLE) {
        Model* m = view_get_model(a->view); m->selection = a->selected; m->tick = 0;
        view_commit_model(a->view, true);
    } else refresh(a);
    return true;
}
static bool custom(void* ctx, uint32_t event) {
    App* a = ctx;
    if(event == 1) {
        open_list(a, Search); refresh(a); view_dispatcher_switch_to_view(a->vd, 0); return true;
    }
    return false;
}
static void load_config(App* a) {
    sf_store_init(&a->config, a->storage, APP_DATA_PATH("config.a"), APP_DATA_PATH("config.b"), 0x53464346);
    sf_copy(a->root, SF_PATH, SF_ROOT); a->language = 0;
    SfRecord config; File* config_file = sf_store_open(&a->config);
    if(sf_record_read(config_file, &config)) {
        sf_copy(a->root, SF_PATH, config.path);
        if(config.plays >= 1 && config.plays <= 3) a->language = config.plays - 1;
    }
    sf_close(config_file);
}
static void initialize(App* a) {
    FuriString* dir = furi_string_alloc_set(APP_DATA_PATH(""));
    storage_common_resolve_path_and_ensure_app_directory(a->storage, dir);
    furi_string_free(dir);

    sf_store_init(&a->meta, a->storage, APP_DATA_PATH("meta.a"), APP_DATA_PATH("meta.b"), 0x53464D45);
    sf_store_init(&a->search, a->storage, APP_DATA_PATH("search.a"), APP_DATA_PATH("search.b"), 0x53465345);
    SfRecord r; File* f = sf_store_open(&a->meta);
    while(sf_record_read(f, &r)) if(r.order > a->order) a->order = r.order;
    sf_close(f);
    a->search_valid = search_matches_root(a);
    a->initialized = true; a->screen = Home;
    if(a->meta.damaged || a->config.damaged || a->search.damaged)
        show_message(a, Home, T("Unvollstaendiger Datenstand"), T("Gueltiger Stand geladen,"), T("falls vorhanden."));
    refresh(a);
}
static void tick(void* ctx) {
    App* a = ctx;
    if(!a->initialized) { initialize(a); return; }
    if(a->screen == Emulate || a->screen == Home) return;
    a->tick++;
    if(a->screen == Scan) { scan_tick(a); refresh(a); }
    else {
        Model* m = view_get_model(a->view); m->tick = a->tick;
        view_commit_model(a->view, true);
    }
}
int32_t storyflip_app(void* p) {
    UNUSED(p);
    App* a = calloc(1, sizeof(App));
    a->gui = furi_record_open(RECORD_GUI);
    a->storage = furi_record_open(RECORD_STORAGE);
    a->dialogs = furi_record_open(RECORD_DIALOGS);
    a->vd = view_dispatcher_alloc(); a->view = view_alloc(); a->text = text_input_alloc();
    view_allocate_model(a->view, ViewModelTypeLocking, sizeof(Model));
    view_set_context(a->view, a); view_set_draw_callback(a->view, draw); view_set_input_callback(a->view, input);
    view_set_previous_callback(text_input_get_view(a->text), query_back);
    view_dispatcher_set_event_callback_context(a->vd, a);
    view_dispatcher_set_custom_event_callback(a->vd, custom);
    view_dispatcher_set_tick_event_callback(a->vd, tick, 150);
    view_dispatcher_add_view(a->vd, 0, a->view);
    view_dispatcher_add_view(a->vd, 1, text_input_get_view(a->text));
    view_dispatcher_attach_to_gui(a->vd, a->gui, ViewDispatcherTypeFullscreen);
    sf_browser_init(&a->browser, a->storage);
    load_config(a);
    a->screen = Splash; refresh(a);
    view_dispatcher_switch_to_view(a->vd, 0); view_dispatcher_run(a->vd);
    emulation_stop(a);
    sf_browser_clear(&a->browser);
    if(a->queue) { a->scan_for_search = false; scan_stop(a, false); }
    storage_common_remove(a->storage, SF_LIST);
    storage_common_remove(a->storage, SF_QUEUE);
    view_dispatcher_remove_view(a->vd, 1); view_dispatcher_remove_view(a->vd, 0);
    text_input_free(a->text); view_free(a->view); view_dispatcher_free(a->vd);
    furi_record_close(RECORD_DIALOGS); furi_record_close(RECORD_STORAGE); furi_record_close(RECORD_GUI);
    free(a); return 0;
}
