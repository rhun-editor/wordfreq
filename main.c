/* wordfreq: the open file's most frequent words as a bar chart in a tab of its own, kept current
 * as you type. Hover a bar for its count; click it to select the word's first use. Written in C
 * on rhunext.h (https://github.com/rhun-editor/extension-protocol/tree/main/c).
 * Protocol 1.1 adds two things that follow the active file: a table of all its words in the
 * bottom panel (a retained view, features view-tree and view-table: rhun draws, sorts and scrolls
 * it, and a click on a row selects the word's first use), and a status bar item with its word
 * count (status-items) that opens the chart; the manifest's item of the same name shows before
 * the program runs. */
#define RHUNEXT_IMPLEMENTATION
#include "rhunext.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP 15          /* the chart's bars */
#define ROWS 200        /* the table's rows */
#define WORD_MAX 64

typedef struct {
    char word[WORD_MAX];
    int count;
    int line, col;      /* its first use */
} entry;

/* the words of a text: every distinct one, most frequent first, then by first use */
typedef struct {
    entry *all;
    size_t n;
    long total;         /* words counted, repeats included */
} tally;

typedef struct {
    int64_t view;       /* 0: no chart open */
    rx_doc doc;         /* the document it charts */
    char name[256];     /* its file name */
    char path[1024];    /* and path ("" when untitled) */
    entry top[TOP];
    int ntop;
    double width, height;
    double adv, lh;     /* the normal text size */
    int hover;          /* the row under the pointer, -1 for none */
    int stale;          /* the document changed: count again before drawing */
    rx_doc active;      /* the active document, 0 for none */
    int64_t table;      /* the table view's instance, 0 before it opens */
    int table_shown;    /* it is on screen */
} state;

/* the documents rhun reported, by id: their paths ("-" when untitled) */
static char paths[256][1024];
static rx_doc ids[256];
static int ndocs;

static const char *path_of(rx_doc d) {
    for (int i = 0; i < ndocs; i++)
        if (ids[i] == d) return paths[i];
    return "-";
}

static void name_of(rx_doc d, char *out, size_t size) {
    const char *path = path_of(d);
    const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
    if (back && (!slash || back > slash)) slash = back;
    snprintf(out, size, "%s", strcmp(path, "-") == 0 ? "untitled" : slash ? slash + 1 : path);
}

static int is_word_byte(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

static int by_count(const void *a, const void *b) {
    const entry *x = (const entry *)a, *y = (const entry *)b;
    if (x->count != y->count) return y->count - x->count;
    return x->line != y->line ? x->line - y->line : x->col - y->col;
}

/* the words of the text, three bytes or longer and not starting with a digit, counted through a
 * hash table of their indexes in t->all, then sorted */
static void count_words(tally *t, const unsigned char *text, size_t len) {
    size_t cap = 1024, nslot = 2048;
    int *slot = (int *)malloc(nslot * sizeof *slot);
    memset(slot, -1, nslot * sizeof *slot);
    t->all = (entry *)calloc(cap, sizeof *t->all);
    t->n = 0;
    t->total = 0;
    int line = 1, col = 1;
    size_t i = 0;
    while (i < len) {
        if (!is_word_byte(text[i])) {
            if (text[i] == '\n') line++, col = 1;
            else col++;
            i++;
            continue;
        }
        size_t start = i;
        int wcol = col;
        while (i < len && is_word_byte(text[i])) i++, col++;
        size_t wl = i - start;
        if (wl < 3 || wl >= WORD_MAX || isdigit(text[start])) continue;
        char w[WORD_MAX];
        uint32_t h = 2166136261u;
        for (size_t k = 0; k < wl; k++) {
            w[k] = (char)tolower(text[start + k]);
            h = (h ^ (unsigned char)w[k]) * 16777619u;
        }
        w[wl] = 0;
        t->total++;
        size_t s = h & (nslot - 1);
        while (slot[s] >= 0 && strcmp(t->all[slot[s]].word, w) != 0) s = (s + 1) & (nslot - 1);
        if (slot[s] >= 0) {
            t->all[slot[s]].count++;
            continue;
        }
        if (t->n == cap) {
            cap *= 2;
            t->all = (entry *)realloc(t->all, cap * sizeof *t->all);
        }
        memcpy(t->all[t->n].word, w, wl + 1);
        t->all[t->n].count = 1;
        t->all[t->n].line = line;
        t->all[t->n].col = wcol;
        slot[s] = (int)t->n++;
        if (t->n * 2 > nslot) {     /* half full: twice the slots, every word placed again */
            nslot *= 2;
            slot = (int *)realloc(slot, nslot * sizeof *slot);
            memset(slot, -1, nslot * sizeof *slot);
            for (size_t k = 0; k < t->n; k++) {
                uint32_t hk = 2166136261u;
                for (const char *c = t->all[k].word; *c; c++) hk = (hk ^ (unsigned char)*c) * 16777619u;
                size_t sk = hk & (nslot - 1);
                while (slot[sk] >= 0) sk = (sk + 1) & (nslot - 1);
                slot[sk] = (int)k;
            }
        }
    }
    free(slot);
    qsort(t->all, t->n, sizeof *t->all, by_count);
}

/* the chart's document counted again: its TOP words */
static void refresh(rx_ext *ext, state *s) {
    size_t len;
    int64_t version;
    unsigned char *text = rx_text(ext, s->doc, &len, &version);
    if (!text) return;
    tally t;
    count_words(&t, text, len);
    free(text);
    s->ntop = t.n < TOP ? (int)t.n : TOP;
    memcpy(s->top, t.all, (size_t)s->ntop * sizeof *s->top);
    free(t.all);
    s->stale = 0;
}

/* the chart: a row per word, its bar as long as its count, the count after it */
static void draw(rx_ext *ext, state *s) {
    if (!s->view || s->width <= 0) return;
    if (s->stale) refresh(ext, s);
    rx_buf p = {NULL, 0, 0};
    double pad = 16, row = s->lh + 10, label_w = 0;
    char title[300];
    snprintf(title, sizeof title, "Most frequent words in %s", s->name);
    rx_label(&p, pad, pad, "large", "left", "fg", title);
    double top = pad + s->lh * 1.6 + 12;
    if (s->ntop == 0) {
        rx_label(&p, pad, top, "normal", "left", "muted", "No words of three letters or more.");
        rx_draw(ext, s->view, &p);
        rx_buf_free(&p);
        return;
    }
    for (int i = 0; i < s->ntop; i++) {
        double w = (double)strlen(s->top[i].word) * s->adv;
        if (w > label_w) label_w = w;
    }
    double x0 = pad + label_w + 12, room = s->width - x0 - pad - 6 * s->adv;
    if (room < 20) room = 20;
    int most = s->top[0].count;
    for (int i = 0; i < s->ntop; i++) {
        double y = top + i * row;
        if (i == s->hover) rx_rrect(&p, pad - 6, y - 4, s->width - 2 * pad + 12, row - 2, 4, "panel");
        rx_label(&p, x0 - 12, y, "normal", "right", i == s->hover ? "fg" : "muted", s->top[i].word);
        double w = room * s->top[i].count / most;
        char series[4];
        snprintf(series, sizeof series, "c%d", i % 8 + 1);
        rx_rrect(&p, x0, y + 2, w < 3 ? 3 : w, s->lh - 4, 3, series);
        char count[32];
        snprintf(count, sizeof count, "%d", s->top[i].count);
        rx_label(&p, x0 + w + 8, y, "normal", "left", "fg", count);
    }
    rx_label(&p, pad, top + s->ntop * row + 8, "small", "left", "muted", "Click a word to select its first use.");
    rx_draw(ext, s->view, &p);
    rx_buf_free(&p);
}

static int row_at(state *s, double y) {
    double top = 16 + s->lh * 1.6 + 12 - 4, row = s->lh + 10;
    if (y < top) return -1;
    int i = (int)((y - top) / row);
    return i < s->ntop ? i : -1;
}

/* Protocol 1.1: the active document's words, in the table (when it is on screen) and as the
 * status item's count; neither shows without an active document. */
static void follow(rx_ext *ext, state *s) {
    int items = rx_has(ext, "status-items"), table = s->table && s->table_shown;
    if (!items && !table) return;
    tally t = {NULL, 0, 0};
    unsigned char *text = NULL;
    size_t len = 0;
    if (s->active) text = rx_text(ext, s->active, &len, NULL);
    if (text) {
        count_words(&t, text, len);
        free(text);
    }
    char name[256];
    name_of(s->active, name, sizeof name);
    if (items && !text) {
        rx_remove_status_item(ext, "count");
    } else if (items) {
        /* "$(list) 12 words", the most frequent in the tooltip; a click opens the chart */
        char label[64], tip[500];
        snprintf(label, sizeof label, "$(list) %ld %s", t.total, t.total == 1 ? "word" : "words");
        int at = snprintf(tip, sizeof tip, "%s: %zu distinct. Most frequent:", name, t.n);
        for (size_t i = 0; i < t.n && i < 5 && at < (int)sizeof tip - 80; i++)
            at += snprintf(tip + at, sizeof tip - at, "%s %s %d", i ? "," : "", t.all[i].word, t.all[i].count);
        snprintf(tip + at, sizeof tip - at, "\nClick for the chart.");
        rx_status_item it = {0};
        it.name = "count";
        it.align = "right";
        it.priority = 10;
        it.text = label;
        it.tooltip = tip;
        it.command = "chart";
        it.when = "activeTab == doc";
        if (rx_set_status_item(ext, &it) < 0) fprintf(stderr, "wordfreq: status item: %s\n", rx_error(ext));
    }
    if (table) {
        rx_patch p;
        memset(&p, 0, sizeof p);
        if (!text) {
            rx_patch_title(&p, "");
            rx_patch_badge(&p, 0);
            rx_patch_kids(&p, "", 0, NULL, 0, 0);
            rx_patch_state(&p, "empty", "Open a file to see its words.", "");
        } else {
            char title[300], msg[200], doc[32], cell[32];
            /* a title is at most 80 bytes: cut a long name at a character's start */
            size_t cut = snprintf(title, sizeof title, "Words: %s", name);
            if (cut > 80) {
                cut = 80;
                while (cut > 0 && ((unsigned char)title[cut] & 0xc0) == 0x80) cut--;
                title[cut] = 0;
            }
            rx_patch_title(&p, title);
            rx_patch_badge(&p, (int)t.n);
            snprintf(msg, sizeof msg, "%ld words, %zu distinct%s", t.total, t.n, t.n > ROWS ? ": the 200 most frequent" : "");
            rx_patch_message(&p, msg);
            rx_column cols[3] = {{"Word", 0, 0, 1}, {"Uses", 1, 6, 1}, {"First line", 1, 10, 1}};
            rx_patch_columns(&p, 3, cols);
            /* a row per word, its id the word; a click opens the document at its first use */
            rx_buf recs = {NULL, 0, 0}, x = {NULL, 0, 0};
            int n = t.n < ROWS ? (int)t.n : ROWS;
            snprintf(doc, sizeof doc, "#%lld", (long long)s->active);
            for (int i = 0; i < n; i++) {
                x.len = 0;
                snprintf(cell, sizeof cell, "%d", t.all[i].count);
                rx_x_cell(&x, cell);
                snprintf(cell, sizeof cell, "%d", t.all[i].line);
                rx_x_cell(&x, cell);
                rx_x_open(&x, doc, t.all[i].line, t.all[i].col);
                rx_node_rec(&recs, t.all[i].word, strlen(t.all[i].word), t.all[i].word, "leaf", &x);
            }
            rx_patch_kids(&p, "", 0, &recs, n, 0);
            if (n == 0) rx_patch_state(&p, "empty", "No words of three letters or more.", "");
            rx_buf_free(&recs);
            rx_buf_free(&x);
        }
        if (rx_view_patch(ext, s->table, &p) < 0) fprintf(stderr, "wordfreq: table: %s\n", rx_error(ext));
        rx_patch_free(&p);
    }
    free(t.all);
}

static void on_command(rx_ext *ext, const char *name, rx_doc doc, void *user) {
    state *s = (state *)user;
    if (strcmp(name, "chart") != 0) return;
    if (!rx_has(ext, "views")) {
        rx_toast(ext, "Word Frequency needs rhun 0.18.0 or later");
        return;
    }
    if (doc == 0) {
        rx_toast(ext, "Open a file first");
        return;
    }
    if (s->view) rx_unview(ext, s->view);
    s->doc = doc;
    snprintf(s->path, sizeof s->path, "%s", path_of(doc));
    name_of(doc, s->name, sizeof s->name);
    s->hover = -1;
    s->stale = 1;
    char title[300];
    snprintf(title, sizeof title, "Words: %s", s->name);
    s->view = rx_view(ext, title);
    if (s->view < 0) {
        s->view = 0;
        rx_toast(ext, rx_error(ext));
    }
}

static void on_event(rx_ext *ext, const rx_event *ev, void *user) {
    state *s = (state *)user;
    if (strcmp(ev->name, "opened") == 0 && ev->argc >= 4) {
        /* opened DOC VERSION LANG PATH: the path may hold spaces */
        int i = ndocs < 256 ? ndocs++ : 255;
        ids[i] = atoll(ev->argv[0]);
        snprintf(paths[i], sizeof paths[i], "%s", rx_event_tail(ev, 3));
    } else if (strcmp(ev->name, "active") == 0 && ev->argc >= 1) {
        s->active = atoll(ev->argv[0]);
        follow(ext, s);
    } else if (strcmp(ev->name, "edits") == 0 && ev->argc >= 1) {
        rx_doc d = atoll(ev->argv[0]);
        if (d == s->doc && s->view) {
            s->stale = 1;
            draw(ext, s);
        }
        if (d == s->active) follow(ext, s);
    }
}

static void on_view(rx_ext *ext, int64_t view, const rx_view_event *ev, void *user) {
    state *s = (state *)user;
    if (view != s->view) return;
    if (strcmp(ev->kind, "size") == 0) {
        s->width = ev->width, s->height = ev->height;
        draw(ext, s);
    } else if (strcmp(ev->kind, "metrics") == 0) {
        s->adv = ev->advance[1], s->lh = ev->line_height[1];
        draw(ext, s);
    } else if (strcmp(ev->kind, "hover") == 0 || strcmp(ev->kind, "leave") == 0) {
        int h = strcmp(ev->kind, "hover") == 0 ? row_at(s, ev->y) : -1;
        if (h != s->hover) {
            s->hover = h;
            draw(ext, s);
        }
    } else if (strcmp(ev->kind, "click") == 0) {
        int i = row_at(s, ev->y);
        if (i >= 0) {
            entry *e = &s->top[i];
            rx_range r = {{e->line, e->col}, {e->line, e->col + (int)strlen(e->word)}};
            /* the file's tab comes forward, then the word is selected in it */
            if (strcmp(s->path, "-") != 0) rx_open(ext, s->path, r.start);
            rx_select(ext, s->doc, r);
        }
    } else if (strcmp(ev->kind, "closed") == 0) {
        s->view = 0;
    }
}

/* the table's instance (the manifest's [views] words), and when it comes on screen */
static void on_view_open(rx_ext *ext, const rx_view_open *open, void *user) {
    state *s = (state *)user;
    (void)ext;
    if (strcmp(open->name, "words") == 0) s->table = open->view;
}

static void on_view_event(rx_ext *ext, int64_t view, const rx_retained_event *ev, void *user) {
    state *s = (state *)user;
    if (view != s->table || strcmp(ev->kind, "visible") != 0) return;
    s->table_shown = ev->on;
    if (ev->on) follow(ext, s);
}

int main(void) {
    static state s;
    s.adv = 7, s.lh = 18, s.hover = -1;
    rx_handlers h = {0};
    h.command = on_command;
    h.event = on_event;
    h.view = on_view;
    h.view_open = on_view_open;
    h.view_event = on_view_event;
    return rx_run("views view-tree view-table status-items", &h, &s);
}
