/* wordfreq: the open file's most frequent words as a bar chart in a tab of its own, kept current
 * as you type. Hover a bar for its count; click it to select the word's first use. Written in C
 * on rhunext.h (https://github.com/rhun-editor/extension-protocol/tree/main/c). */
#define RHUNEXT_IMPLEMENTATION
#include "rhunext.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP 15
#define WORD_MAX 64

typedef struct {
    char word[WORD_MAX];
    int count;
    int line, col;      /* its first use */
} entry;

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

static int is_word_byte(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

/* the words of the text, three bytes or longer, the TOP most frequent */
static void count_words(state *s, const unsigned char *text, size_t len) {
    size_t cap = 1024, n = 0;
    entry *all = (entry *)calloc(cap, sizeof *all);
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
        for (size_t k = 0; k < wl; k++) w[k] = (char)tolower(text[start + k]);
        w[wl] = 0;
        size_t k;
        for (k = 0; k < n; k++)
            if (strcmp(all[k].word, w) == 0) break;
        if (k < n) {
            all[k].count++;
            continue;
        }
        if (n == cap) {
            cap *= 2;
            all = (entry *)realloc(all, cap * sizeof *all);
        }
        memcpy(all[n].word, w, wl + 1);
        all[n].count = 1;
        all[n].line = line;
        all[n].col = wcol;
        n++;
    }
    /* the top ones, most frequent first, then by first use */
    s->ntop = 0;
    for (int t = 0; t < TOP; t++) {
        int best = -1;
        for (size_t k = 0; k < n; k++)
            if (all[k].count > 0 && (best < 0 || all[k].count > all[best].count)) best = (int)k;
        if (best < 0) break;
        s->top[s->ntop++] = all[best];
        all[best].count = 0;
    }
    free(all);
}

static void refresh(rx_ext *ext, state *s) {
    size_t len;
    int64_t version;
    unsigned char *text = rx_text(ext, s->doc, &len, &version);
    if (!text) return;
    count_words(s, text, len);
    free(text);
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
    s->hover = -1;
    s->stale = 1;
    char title[300];
    snprintf(title, sizeof title, "Words: %s", s->name[0] ? s->name : "untitled");
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
        const char *path = path_of(atoll(ev->argv[0]));
        const char *slash = strrchr(path, '/'), *back = strrchr(path, '\\');
        if (back && (!slash || back > slash)) slash = back;
        snprintf(s->name, sizeof s->name, "%s", strcmp(path, "-") == 0 ? "untitled" : slash ? slash + 1 : path);
    } else if (strcmp(ev->name, "edits") == 0 && ev->argc >= 1 && atoll(ev->argv[0]) == s->doc && s->view) {
        s->stale = 1;
        draw(ext, s);
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

int main(void) {
    static state s;
    s.adv = 7, s.lh = 18, s.hover = -1;
    rx_handlers h = {0};
    h.command = on_command;
    h.event = on_event;
    h.view = on_view;
    return rx_run("views", &h, &s);
}
