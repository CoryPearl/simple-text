/*
    main.c - a minimal raylib text/Markdown editor.

    Build:
      ./run.sh

    Run:
      ./main [file]

    Controls:
      Cmd+O  open a file
      Cmd+S  save
      Cmd+Shift+S  save as
      Cmd+N  new file
      Cmd+L  toggle light/dark mode
      Cmd+P  toggle Markdown text/preview
      Cmd +/- zoom text, Cmd+0 reset zoom
      Cmd+A  select all
      Tab  insert 5 spaces
      Click+drag or Shift+arrows  select text
      Cmd+C/X/V  copy/cut/paste selection (or current line if nothing selected)
      Trackpad swipe or Shift+scroll to scroll left/right (only when a line overflows the view)
      Drag and drop a .md, .markdown, or .txt file to open it.
      Double-click or use Open With in Finder to open files here, once packaged
      as an .app with the Info.plist in this project (see package_mac_app.sh).
*/

#include "raylib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#endif

#define APP_TITLE "Simple Txt"
#define INITIAL_LINE_CAP 128
#define MAX_PATH_EDIT 1024
#define STATUS_TIME 1.1f
#define KEY_REPEAT_DELAY 0.34
#define KEY_REPEAT_RATE 0.045
#define TEXT_SPACING 2.2f
#define STATUS_TEXT_SPACING 1.2f
#define BUTTON_TEXT_SPACING 1.0f
#define RENDER_LINE_CAP 4096
#define TAB_WIDTH 5

typedef struct {
    char *text;
    int len;
    int cap;
} Line;

typedef struct {
    Line *lines;
    int count;
    int cap;
    int row;
    int col;
    int preferred_col;
    int first_line;
    float x_scroll;
    char path[MAX_PATH_EDIT];
    int dirty;
    int markdown;
    int sel_active;
    int sel_anchor_row;
    int sel_anchor_col;
} Editor;

typedef struct {
    Color bg;
    Color panel;
    Color panel2;
    Color text;
    Color muted;
    Color faint;
    Color border;
    Color accent;
    Color heading;
    Color marker;
    Color code;
    Color quote;
    Color link;
    Color selection;
    Color danger;
} Theme;

typedef struct {
    float x;
    float y;
} DrawPos;

typedef enum {
    PROMPT_NONE,
    PROMPT_OPEN,
    PROMPT_SAVE_AS
} PromptMode;

static char prompt_text[MAX_PATH_EDIT];
static int prompt_len = 0;
static PromptMode prompt_mode = PROMPT_NONE;
static char status_msg[512] = "Ready";
static float status_timer = 0.0f;
static int preview_mode = 0;
static int dark_mode = 1;
static float editor_font_size = 18.0f;
static int preview_link_opened = 0;
static int mouse_selecting = 0;

static const char *base_name(const char *path);

static char *str_dup_c(const char *s) {
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) exit(1);
    memcpy(out, s, n + 1);
    return out;
}

static void set_status(const char *msg) {
    snprintf(status_msg, sizeof(status_msg), "%s", msg);
    status_timer = STATUS_TIME;
}

static int has_supported_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 0;
    char ext[32] = {0};
    int i = 0;
    for (; dot[i] && i < (int)sizeof(ext) - 1; i++) ext[i] = (char)tolower((unsigned char)dot[i]);
    return strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0 || strcmp(ext, ".txt") == 0;
}

static int has_any_ext(const char *path) {
    const char *name = base_name(path);
    const char *dot = strrchr(name, '.');
    return dot && dot[1] != '\0';
}

static int is_markdown_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return 1;
    char ext[32] = {0};
    int i = 0;
    for (; dot[i] && i < (int)sizeof(ext) - 1; i++) ext[i] = (char)tolower((unsigned char)dot[i]);
    return strcmp(ext, ".md") == 0 || strcmp(ext, ".markdown") == 0;
}

static const char *base_name(const char *path) {
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');
    const char *p = a > b ? a : b;
    return p ? p + 1 : path;
}

static void line_init(Line *line, const char *text) {
    int len = text ? (int)strlen(text) : 0;
    line->cap = len + 32;
    if (line->cap < 32) line->cap = 32;
    line->text = (char *)malloc((size_t)line->cap);
    if (!line->text) exit(1);
    if (len > 0) memcpy(line->text, text, (size_t)len);
    line->text[len] = '\0';
    line->len = len;
}

static void line_free(Line *line) {
    free(line->text);
    line->text = NULL;
    line->len = 0;
    line->cap = 0;
}

static void line_reserve(Line *line, int need) {
    if (need <= line->cap) return;
    while (line->cap < need) line->cap *= 2;
    line->text = (char *)realloc(line->text, (size_t)line->cap);
    if (!line->text) exit(1);
}

static void line_insert_text(Line *line, int col, const char *text, int len) {
    if (col < 0) col = 0;
    if (col > line->len) col = line->len;
    line_reserve(line, line->len + len + 1);
    memmove(line->text + col + len, line->text + col, (size_t)(line->len - col + 1));
    memcpy(line->text + col, text, (size_t)len);
    line->len += len;
}

static void line_delete_range(Line *line, int col, int len) {
    if (col < 0 || col >= line->len || len <= 0) return;
    if (col + len > line->len) len = line->len - col;
    memmove(line->text + col, line->text + col + len, (size_t)(line->len - col - len + 1));
    line->len -= len;
}

static void editor_init(Editor *e) {
    e->cap = INITIAL_LINE_CAP;
    e->lines = (Line *)malloc(sizeof(Line) * (size_t)e->cap);
    if (!e->lines) exit(1);
    e->count = 1;
    line_init(&e->lines[0], "");
    e->row = 0;
    e->col = 0;
    e->preferred_col = 0;
    e->first_line = 0;
    e->x_scroll = 0;
    e->path[0] = '\0';
    e->dirty = 0;
    e->markdown = 1;
    e->sel_active = 0;
    e->sel_anchor_row = 0;
    e->sel_anchor_col = 0;
}

static void editor_clear(Editor *e) {
    for (int i = 0; i < e->count; i++) line_free(&e->lines[i]);
    e->count = 1;
    line_init(&e->lines[0], "");
    e->row = 0;
    e->col = 0;
    e->preferred_col = 0;
    e->first_line = 0;
    e->x_scroll = 0;
    e->path[0] = '\0';
    e->dirty = 0;
    e->markdown = 1;
    e->sel_active = 0;
    e->sel_anchor_row = 0;
    e->sel_anchor_col = 0;
}

static void editor_free(Editor *e) {
    for (int i = 0; i < e->count; i++) line_free(&e->lines[i]);
    free(e->lines);
}

static void editor_reserve_lines(Editor *e, int need) {
    if (need <= e->cap) return;
    while (e->cap < need) e->cap *= 2;
    e->lines = (Line *)realloc(e->lines, sizeof(Line) * (size_t)e->cap);
    if (!e->lines) exit(1);
}

static void editor_insert_line(Editor *e, int row, const char *text) {
    if (row < 0) row = 0;
    if (row > e->count) row = e->count;
    editor_reserve_lines(e, e->count + 1);
    memmove(&e->lines[row + 1], &e->lines[row], sizeof(Line) * (size_t)(e->count - row));
    line_init(&e->lines[row], text);
    e->count++;
}

static void editor_remove_line(Editor *e, int row) {
    if (e->count <= 1 || row < 0 || row >= e->count) return;
    line_free(&e->lines[row]);
    memmove(&e->lines[row], &e->lines[row + 1], sizeof(Line) * (size_t)(e->count - row - 1));
    e->count--;
}

static void editor_new(Editor *e) {
    editor_clear(e);
    set_status("New untitled document");
}

static int editor_load(Editor *e, const char *path) {
    if (!has_supported_ext(path)) {
        set_status("Only .md, .markdown, and .txt files are supported");
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        set_status("Could not open file");
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        set_status("Could not read file");
        return 0;
    }

    char *data = (char *)malloc((size_t)size + 1);
    if (!data) exit(1);
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    data[got] = '\0';

    for (int i = 0; i < e->count; i++) line_free(&e->lines[i]);
    e->count = 0;

    char *start = data;
    for (size_t i = 0; i <= got; i++) {
        if (data[i] == '\n' || data[i] == '\0') {
            char saved = data[i];
            data[i] = '\0';
            int len = (int)strlen(start);
            if (len > 0 && start[len - 1] == '\r') start[len - 1] = '\0';
            editor_insert_line(e, e->count, start);
            data[i] = saved;
            start = data + i + 1;
        }
    }
    if (e->count == 0) editor_insert_line(e, 0, "");

    snprintf(e->path, sizeof(e->path), "%s", path);
    e->row = 0;
    e->col = 0;
    e->preferred_col = 0;
    e->first_line = 0;
    e->x_scroll = 0;
    e->dirty = 0;
    e->markdown = is_markdown_path(path);
    free(data);
    set_status("Opened file");
    return 1;
}

static int editor_save_as(Editor *e, const char *path) {
    if (!has_supported_ext(path)) {
        set_status("Save path must end in .md, .markdown, or .txt");
        return 0;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        set_status("Could not save file");
        return 0;
    }

    for (int i = 0; i < e->count; i++) {
        fwrite(e->lines[i].text, 1, (size_t)e->lines[i].len, f);
        if (i < e->count - 1) fputc('\n', f);
    }
    fclose(f);

    snprintf(e->path, sizeof(e->path), "%s", path);
    e->dirty = 0;
    e->markdown = is_markdown_path(path);
    set_status("Saved");
    return 1;
}

static int editor_save(Editor *e) {
    if (e->path[0] == '\0') return 0;
    return editor_save_as(e, e->path);
}

static int read_dialog_path(const char *command, char *out, size_t out_size) {
    if (out_size == 0) return 0;
    out[0] = '\0';

#if defined(__APPLE__)
    FILE *pipe = popen(command, "r");
    if (!pipe) return 0;
    if (!fgets(out, (int)out_size, pipe)) {
        pclose(pipe);
        out[0] = '\0';
        return 0;
    }
    int status = pclose(pipe);
    if (status != 0) {
        out[0] = '\0';
        return 0;
    }

    int len = (int)strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
    return len > 0;
#else
    (void)command;
    return 0;
#endif
}

static int open_file_dialog(Editor *e) {
    char path[MAX_PATH_EDIT];
    const char *command =
        "osascript "
        "-e 'set chosenFile to choose file with prompt \"Open text or Markdown file\"' "
        "-e 'POSIX path of chosenFile'";

    if (!read_dialog_path(command, path, sizeof(path))) {
        set_status("Open cancelled");
        return 0;
    }
    return editor_load(e, path);
}

static int save_file_dialog(Editor *e) {
    char path[MAX_PATH_EDIT];
    const char *command =
        "osascript "
        "-e 'set chosenFile to choose file name with prompt \"Save Markdown file\" default name \"Untitled.md\"' "
        "-e 'POSIX path of chosenFile'";

    if (!read_dialog_path(command, path, sizeof(path))) {
        set_status("Save cancelled");
        return 0;
    }

    if (!has_any_ext(path)) {
        size_t len = strlen(path);
        if (len + 3 < sizeof(path)) strcat(path, ".md");
    }
    return editor_save_as(e, path);
}

static void clamp_cursor(Editor *e) {
    if (e->row < 0) e->row = 0;
    if (e->row >= e->count) e->row = e->count - 1;
    if (e->col < 0) e->col = 0;
    if (e->col > e->lines[e->row].len) e->col = e->lines[e->row].len;
}

static void move_left(Editor *e) {
    if (e->col > 0) e->col--;
    else if (e->row > 0) {
        e->row--;
        e->col = e->lines[e->row].len;
    }
    e->preferred_col = e->col;
}

static void move_right(Editor *e) {
    if (e->col < e->lines[e->row].len) e->col++;
    else if (e->row < e->count - 1) {
        e->row++;
        e->col = 0;
    }
    e->preferred_col = e->col;
}

static void move_up(Editor *e) {
    if (e->row > 0) e->row--;
    e->col = e->preferred_col;
    clamp_cursor(e);
}

static void move_down(Editor *e) {
    if (e->row < e->count - 1) e->row++;
    e->col = e->preferred_col;
    clamp_cursor(e);
}

static void insert_char(Editor *e, int ch) {
    if (ch < 32 || ch == 127) return;
    char c = (char)ch;
    line_insert_text(&e->lines[e->row], e->col, &c, 1);
    e->col++;
    e->preferred_col = e->col;
    e->dirty = 1;
}

static void insert_newline(Editor *e) {
    Line *line = &e->lines[e->row];
    char *right = str_dup_c(line->text + e->col);
    line->text[e->col] = '\0';
    line->len = e->col;
    editor_insert_line(e, e->row + 1, right);
    free(right);
    e->row++;
    e->col = 0;
    e->preferred_col = 0;
    e->dirty = 1;
}

static void backspace(Editor *e) {
    if (e->col > 0) {
        line_delete_range(&e->lines[e->row], e->col - 1, 1);
        e->col--;
    } else if (e->row > 0) {
        int old_len = e->lines[e->row - 1].len;
        line_insert_text(&e->lines[e->row - 1], old_len, e->lines[e->row].text, e->lines[e->row].len);
        editor_remove_line(e, e->row);
        e->row--;
        e->col = old_len;
    } else {
        return;
    }
    e->preferred_col = e->col;
    e->dirty = 1;
}

static void delete_forward(Editor *e) {
    if (e->col < e->lines[e->row].len) {
        line_delete_range(&e->lines[e->row], e->col, 1);
    } else if (e->row < e->count - 1) {
        line_insert_text(&e->lines[e->row], e->lines[e->row].len, e->lines[e->row + 1].text, e->lines[e->row + 1].len);
        editor_remove_line(e, e->row + 1);
    } else {
        return;
    }
    e->dirty = 1;
}

static void paste_text(Editor *e, const char *text) {
    if (!text) return;
    for (const char *p = text; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n') insert_newline(e);
        else insert_char(e, (unsigned char)*p);
    }
}

static char *current_line_clip(Editor *e, int include_newline) {
    Line *line = &e->lines[e->row];
    int extra = include_newline ? 1 : 0;
    char *out = (char *)malloc((size_t)line->len + extra + 1);
    if (!out) exit(1);
    memcpy(out, line->text, (size_t)line->len);
    if (include_newline) out[line->len] = '\n';
    out[line->len + extra] = '\0';
    return out;
}

static int has_selection(Editor *e) {
    return e->sel_active && (e->sel_anchor_row != e->row || e->sel_anchor_col != e->col);
}

static void clear_selection(Editor *e) {
    e->sel_active = 0;
}

static void selection_range(Editor *e, int *r0, int *c0, int *r1, int *c1) {
    if (e->sel_anchor_row < e->row || (e->sel_anchor_row == e->row && e->sel_anchor_col < e->col)) {
        *r0 = e->sel_anchor_row; *c0 = e->sel_anchor_col;
        *r1 = e->row; *c1 = e->col;
    } else {
        *r0 = e->row; *c0 = e->col;
        *r1 = e->sel_anchor_row; *c1 = e->sel_anchor_col;
    }
}

static char *selection_text(Editor *e) {
    if (!has_selection(e)) return str_dup_c("");
    int r0, c0, r1, c1;
    selection_range(e, &r0, &c0, &r1, &c1);

    size_t total;
    if (r0 == r1) {
        total = (size_t)(c1 - c0);
    } else {
        total = (size_t)(e->lines[r0].len - c0) + 1;
        for (int r = r0 + 1; r < r1; r++) total += (size_t)e->lines[r].len + 1;
        total += (size_t)c1;
    }

    char *out = (char *)malloc(total + 1);
    if (!out) exit(1);
    size_t pos = 0;
    if (r0 == r1) {
        memcpy(out, e->lines[r0].text + c0, total);
        pos = total;
    } else {
        Line *l0 = &e->lines[r0];
        size_t n = (size_t)(l0->len - c0);
        memcpy(out + pos, l0->text + c0, n);
        pos += n;
        out[pos++] = '\n';
        for (int r = r0 + 1; r < r1; r++) {
            Line *l = &e->lines[r];
            memcpy(out + pos, l->text, (size_t)l->len);
            pos += (size_t)l->len;
            out[pos++] = '\n';
        }
        memcpy(out + pos, e->lines[r1].text, (size_t)c1);
        pos += (size_t)c1;
    }
    out[pos] = '\0';
    return out;
}

static void selection_delete(Editor *e) {
    if (!has_selection(e)) return;
    int r0, c0, r1, c1;
    selection_range(e, &r0, &c0, &r1, &c1);

    if (r0 == r1) {
        line_delete_range(&e->lines[r0], c0, c1 - c0);
    } else {
        Line *l0 = &e->lines[r0];
        Line *l1 = &e->lines[r1];
        int tail_len = l1->len - c1;
        l0->len = c0;
        l0->text[c0] = '\0';
        line_insert_text(l0, c0, l1->text + c1, tail_len);
        for (int r = r1; r > r0; r--) editor_remove_line(e, r);
    }

    e->row = r0;
    e->col = c0;
    e->preferred_col = c0;
    e->sel_active = 0;
    e->dirty = 1;
}

static void select_all(Editor *e) {
    e->sel_anchor_row = 0;
    e->sel_anchor_col = 0;
    e->row = e->count - 1;
    e->col = e->lines[e->count - 1].len;
    e->preferred_col = e->col;
    e->sel_active = 1;
}

static int line_is_fence(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return strncmp(s, "```", 3) == 0 || strncmp(s, "~~~", 3) == 0;
}

#define MAX_TABLE_COLS 10
#define MAX_TABLE_ROWS 48
#define MAX_TABLE_CELL 128

static int line_is_table_row(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return *s == '|';
}

static int line_is_table_separator(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return 0;
    int has_dash = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '-') has_dash = 1;
        else if (*p != '|' && *p != ':' && *p != ' ' && *p != '\t') return 0;
    }
    return has_dash;
}

static int parse_table_row(const char *line, char cells[][MAX_TABLE_CELL], int max_cols) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", line);
    char *s = buf;
    while (*s == ' ' || *s == '\t') s++;
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) { s[--len] = '\0'; }
    if (*s == '|') s++;

    int n = 0;
    char *tok = s;
    while (n < max_cols) {
        char *pipe = strchr(tok, '|');
        int clen = pipe ? (int)(pipe - tok) : (int)strlen(tok);
        if (clen > MAX_TABLE_CELL - 1) clen = MAX_TABLE_CELL - 1;
        char cell[MAX_TABLE_CELL];
        memcpy(cell, tok, (size_t)clen);
        cell[clen] = '\0';

        char *cs = cell;
        while (*cs == ' ' || *cs == '\t') cs++;
        int cl = (int)strlen(cs);
        while (cl > 0 && (cs[cl - 1] == ' ' || cs[cl - 1] == '\t')) { cs[--cl] = '\0'; }
        snprintf(cells[n], MAX_TABLE_CELL, "%s", cs);
        n++;

        if (!pipe) break;
        tok = pipe + 1;
        if (*tok == '\0') break;
    }
    return n;
}

static int in_code_block(Editor *e, int row) {
    int in = 0;
    for (int i = 0; i < row; i++) {
        if (line_is_fence(e->lines[i].text)) in = !in;
    }
    return in;
}

static Color syntax_color(Editor *e, int row, int col, Theme t) {
    if (!e->markdown) return t.text;
    char *s = e->lines[row].text;
    int len = e->lines[row].len;
    int p = 0;
    while (s[p] == ' ' || s[p] == '\t') p++;

    if (in_code_block(e, row) || line_is_fence(s)) return t.code;
    if (s[p] == '#') return t.heading;
    if (s[p] == '>') return col == p ? t.marker : t.quote;
    if ((s[p] == '-' || s[p] == '*' || s[p] == '+') && s[p + 1] == ' ') return col <= p ? t.marker : t.text;
    if (isdigit((unsigned char)s[p])) {
        int q = p;
        while (isdigit((unsigned char)s[q])) q++;
        if (s[q] == '.' && s[q + 1] == ' ') return col <= q ? t.marker : t.text;
    }

    int tick = 0;
    for (int i = 0; i <= col && i < len; i++) if (s[i] == '`') tick = !tick;
    if (tick || s[col] == '`') return t.code;

    int link_depth = 0;
    for (int i = 0; i <= col && i < len; i++) {
        if (s[i] == '[' || s[i] == ']') link_depth = (s[i] == '[');
        if (i == col && (link_depth || s[i] == '[' || s[i] == ']')) return t.link;
    }
    if (s[col] == '*' || s[col] == '_' || s[col] == '[' || s[col] == ']' || s[col] == '(' || s[col] == ')') return t.marker;
    return t.text;
}

static void compute_line_colors(Editor *e, int row, int in_code, Theme t, Color *out, int len) {
    Line *line = &e->lines[row];
    char *s = line->text;

    if (!e->markdown) {
        for (int c = 0; c < len; c++) out[c] = t.text;
        return;
    }

    int p = 0;
    while (s[p] == ' ' || s[p] == '\t') p++;

    if (in_code || line_is_fence(s)) {
        for (int c = 0; c < len; c++) out[c] = t.code;
        return;
    }
    if (s[p] == '#') {
        for (int c = 0; c < len; c++) out[c] = t.heading;
        return;
    }
    if (s[p] == '>') {
        for (int c = 0; c < len; c++) out[c] = (c == p) ? t.marker : t.quote;
        return;
    }
    if ((s[p] == '-' || s[p] == '*' || s[p] == '+') && s[p + 1] == ' ') {
        for (int c = 0; c < len; c++) out[c] = (c <= p) ? t.marker : t.text;
        return;
    }
    if (isdigit((unsigned char)s[p])) {
        int q = p;
        while (isdigit((unsigned char)s[q])) q++;
        if (s[q] == '.' && s[q + 1] == ' ') {
            for (int c = 0; c < len; c++) out[c] = (c <= q) ? t.marker : t.text;
            return;
        }
    }

    int tick = 0;
    int link_depth = 0;
    for (int c = 0; c < len; c++) {
        char ch = s[c];
        if (ch == '`') tick = !tick;
        if (tick || ch == '`') { out[c] = t.code; continue; }
        if (ch == '[' || ch == ']') link_depth = (ch == '[');
        if (link_depth || ch == '[' || ch == ']') { out[c] = t.link; continue; }
        if (ch == '*' || ch == '_' || ch == '[' || ch == ']' || ch == '(' || ch == ')') { out[c] = t.marker; continue; }
        out[c] = t.text;
    }
}

static Theme get_theme(void) {
    if (dark_mode) {
        return (Theme){
            .bg = {18, 18, 18, 255},
            .panel = {29, 29, 29, 255},
            .panel2 = {42, 42, 42, 255},
            .text = {236, 236, 236, 255},
            .muted = {166, 166, 166, 255},
            .faint = {88, 88, 88, 255},
            .border = {64, 64, 64, 255},
            .accent = {246, 246, 246, 255},
            .heading = {124, 190, 255, 255},
            .marker = {246, 180, 103, 255},
            .code = {155, 219, 126, 255},
            .quote = {177, 164, 224, 255},
            .link = {96, 203, 255, 255},
            .selection = {70, 70, 70, 255},
            .danger = {220, 220, 220, 255},
        };
    }

    return (Theme){
        .bg = {252, 252, 252, 255},
        .panel = {242, 242, 242, 255},
        .panel2 = {232, 232, 232, 255},
        .text = {24, 24, 24, 255},
        .muted = {92, 92, 92, 255},
        .faint = {188, 188, 188, 255},
        .border = {204, 204, 204, 255},
        .accent = {20, 20, 20, 255},
        .heading = {15, 98, 169, 255},
        .marker = {172, 92, 22, 255},
        .code = {42, 122, 49, 255},
        .quote = {103, 84, 166, 255},
        .link = {0, 110, 185, 255},
        .selection = {224, 224, 224, 255},
        .danger = {70, 70, 70, 255},
    };
}

static void zoom_text(float delta) {
    editor_font_size += delta;
    if (editor_font_size < 12.0f) editor_font_size = 12.0f;
    if (editor_font_size > 34.0f) editor_font_size = 34.0f;
}

static Font load_app_font(int size, int *custom_font) {
    Font font = GetFontDefault();
    *custom_font = 0;

#if defined(__APPLE__)
    if (FileExists("/System/Library/Fonts/Menlo.ttc")) {
        font = LoadFontEx("/System/Library/Fonts/Menlo.ttc", size, NULL, 0);
        SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
        *custom_font = 1;
    }
#else
    (void)size;
#endif

    return font;
}

static int key_action(int key) {
    static double next_time[512] = {0};
    double now = GetTime();

    if (IsKeyPressed(key)) {
        if (key >= 0 && key < 512) next_time[key] = now + KEY_REPEAT_DELAY;
        return 1;
    }
    if (IsKeyDown(key) && key >= 0 && key < 512 && now >= next_time[key]) {
        next_time[key] = now + KEY_REPEAT_RATE;
        return 1;
    }
    return 0;
}

static float glyph_advance(Font font, char c, float size) {
    if (c == '\t') return (MeasureTextEx(font, "n", size, TEXT_SPACING).x * 0.55f + TEXT_SPACING) * TAB_WIDTH;
    if (c == ' ') return MeasureTextEx(font, "n", size, TEXT_SPACING).x * 0.55f + TEXT_SPACING;

    char s[2] = {c, 0};
    return MeasureTextEx(font, s, size, TEXT_SPACING).x + TEXT_SPACING;
}

static int column_from_x(Line *line, Font font, float font_size, float target_x) {
    if (target_x <= 0) return 0;

    float x = 0;
    for (int i = 0; i < line->len; i++) {
        float w = glyph_advance(font, line->text[i], font_size);
        if (target_x < x + w * 0.5f) return i;
        if (target_x < x + w) return i + 1;
        x += w;
    }
    return line->len;
}

static char *trim_markdown_prefix(char *s, int *heading_level, int *quote, int *bullet) {
    *heading_level = 0;
    *quote = 0;
    *bullet = 0;
    while (*s == ' ' || *s == '\t') s++;

    while (*s == '#' && *heading_level < 6) {
        (*heading_level)++;
        s++;
    }
    if (*heading_level > 0 && *s == ' ') {
        while (*s == ' ') s++;
        return s;
    }
    *heading_level = 0;

    if (*s == '>') {
        *quote = 1;
        s++;
        if (*s == ' ') s++;
        return s;
    }
    if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
        *bullet = 1;
        return s + 2;
    }
    if (isdigit((unsigned char)s[0])) {
        char *p = s;
        while (isdigit((unsigned char)*p)) p++;
        if (*p == '.' && p[1] == ' ') {
            *bullet = 1;
            return p + 2;
        }
    }
    return s;
}

static int button(Rectangle r, const char *label, Font font, float size, Theme t) {
    Vector2 m = GetMousePosition();
    int hover = CheckCollisionPointRec(m, r);
    DrawRectangleRounded(r, 0.18f, 8, hover ? t.panel2 : t.panel);
    DrawRectangleRoundedLinesEx(r, 0.18f, 8, 1, hover ? t.accent : t.border);
    Vector2 ts = MeasureTextEx(font, label, size, BUTTON_TEXT_SPACING);
    DrawTextEx(font, label, (Vector2){r.x + (r.width - ts.x) * 0.5f, r.y + (r.height - ts.y) * 0.5f - 1}, size, BUTTON_TEXT_SPACING, t.text);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static int theme_toggle(Rectangle r, Font font, float size, Theme t) {
    Vector2 m = GetMousePosition();
    int hover = CheckCollisionPointRec(m, r);
    const char *label = dark_mode ? "Dark" : "Light";
    Color track = hover ? t.panel2 : t.panel;
    Color knob = dark_mode ? t.accent : t.bg;
    float knob_size = r.height - 8;
    float knob_x = dark_mode ? r.x + r.width - knob_size - 4 : r.x + 4;

    DrawRectangleRounded(r, 0.5f, 16, track);
    DrawRectangleRoundedLinesEx(r, 0.5f, 16, 1, hover ? t.accent : t.border);
    DrawCircleV((Vector2){knob_x + knob_size * 0.5f, r.y + r.height * 0.5f}, knob_size * 0.5f, knob);
    DrawCircleLines((int)(knob_x + knob_size * 0.5f), (int)(r.y + r.height * 0.5f), knob_size * 0.5f, t.border);

    Vector2 ts = MeasureTextEx(font, label, size, BUTTON_TEXT_SPACING);
    float tx = dark_mode ? r.x + 12 : r.x + r.width - ts.x - 12;
    DrawTextEx(font, label, (Vector2){tx, r.y + (r.height - ts.y) * 0.5f - 1}, size, BUTTON_TEXT_SPACING, t.text);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void begin_prompt(PromptMode mode, const char *initial) {
    prompt_mode = mode;
    snprintf(prompt_text, sizeof(prompt_text), "%s", initial ? initial : "");
    prompt_len = (int)strlen(prompt_text);
}

static void cancel_prompt(void) {
    prompt_mode = PROMPT_NONE;
    prompt_text[0] = '\0';
    prompt_len = 0;
}

static void ensure_cursor_visible(Editor *e, int visible_lines, float editor_w, float gutter_w, float char_w) {
    if (e->row < e->first_line) e->first_line = e->row;
    if (e->row >= e->first_line + visible_lines) e->first_line = e->row - visible_lines + 1;
    if (e->first_line < 0) e->first_line = 0;

    float cursor_x = e->col * char_w;
    float area_w = editor_w - gutter_w - 28;
    if (cursor_x - e->x_scroll > area_w) e->x_scroll = cursor_x - area_w + char_w;
    if (cursor_x - e->x_scroll < 0) e->x_scroll = cursor_x;
    if (e->x_scroll < 0) e->x_scroll = 0;
}

static void clamp_scroll(Editor *e, int visible_lines) {
    int max_first = e->count - visible_lines;
    if (max_first < 0) max_first = 0;
    if (e->first_line < 0) e->first_line = 0;
    if (e->first_line > max_first) e->first_line = max_first;
}

static int max_line_len(Editor *e) {
    int m = 0;
    for (int i = 0; i < e->count; i++) if (e->lines[i].len > m) m = e->lines[i].len;
    return m;
}

static float compute_max_x_scroll(Editor *e, float visible_text_w, float char_w) {
    float content_w = (float)max_line_len(e) * (char_w + TEXT_SPACING);
    float max_scroll = content_w - visible_text_w;
    return max_scroll > 0 ? max_scroll : 0;
}

static void handle_prompt(Editor *e) {
    int ch;
    while ((ch = GetCharPressed()) > 0) {
        if (ch >= 32 && ch < 127 && prompt_len < MAX_PATH_EDIT - 1) {
            prompt_text[prompt_len++] = (char)ch;
            prompt_text[prompt_len] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && prompt_len > 0) prompt_text[--prompt_len] = '\0';
    if (IsKeyPressed(KEY_ESCAPE)) cancel_prompt();
    if (IsKeyPressed(KEY_ENTER)) {
        if (prompt_mode == PROMPT_OPEN) editor_load(e, prompt_text);
        if (prompt_mode == PROMPT_SAVE_AS) editor_save_as(e, prompt_text);
        cancel_prompt();
    }
}

static void handle_editor_input(Editor *e, Rectangle edit_area, Font font, float font_size, float gutter_w, float char_w, float line_h) {
    int mod = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    int visible_lines = (int)((edit_area.height - 16) / line_h);
    int keep_cursor_visible = 0;

    if (mod && IsKeyPressed(KEY_O)) open_file_dialog(e);
    else if (mod && shift && IsKeyPressed(KEY_S)) save_file_dialog(e);
    else if (mod && IsKeyPressed(KEY_S)) {
        if (!editor_save(e)) save_file_dialog(e);
    } else if (mod && IsKeyPressed(KEY_N)) editor_new(e);
    else if (mod && IsKeyPressed(KEY_L)) {
        dark_mode = !dark_mode;
        set_status(dark_mode ? "Dark mode" : "Light mode");
    }
    else if (mod && IsKeyPressed(KEY_P)) {
        preview_mode = !preview_mode;
        set_status(preview_mode ? "Showing Markdown preview" : "Showing Markdown text");
    } else if (mod && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))) zoom_text(2.0f);
    else if (mod && (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))) zoom_text(-2.0f);
    else if (mod && IsKeyPressed(KEY_ZERO)) editor_font_size = 18.0f;
    else if (mod && IsKeyPressed(KEY_A)) {
        select_all(e);
        set_status("Selected all");
    } else if (mod && IsKeyPressed(KEY_C)) {
        char *clip = has_selection(e) ? selection_text(e) : current_line_clip(e, 0);
        SetClipboardText(clip);
        free(clip);
        set_status(has_selection(e) ? "Copied selection" : "Copied current line");
    } else if (mod && IsKeyPressed(KEY_X)) {
        if (has_selection(e)) {
            char *clip = selection_text(e);
            SetClipboardText(clip);
            free(clip);
            selection_delete(e);
            set_status("Cut selection");
        } else {
            char *clip = current_line_clip(e, 1);
            SetClipboardText(clip);
            free(clip);
            if (e->count > 1) {
                editor_remove_line(e, e->row);
                if (e->row >= e->count) e->row = e->count - 1;
                e->col = 0;
            } else {
                e->lines[0].text[0] = '\0';
                e->lines[0].len = 0;
                e->col = 0;
            }
            e->dirty = 1;
            set_status("Cut current line");
        }
    } else if (mod && IsKeyPressed(KEY_V)) {
        if (has_selection(e)) selection_delete(e);
        paste_text(e, GetClipboardText());
        set_status("Pasted");
    }

    static float wheel_accum = 0.0f;
    Vector2 wheel = GetMouseWheelMoveV();
    float wheel_y = wheel.y;
    float wheel_x = wheel.x;
    if (shift && wheel_x == 0 && wheel_y != 0) { wheel_x = wheel_y; wheel_y = 0; }

    if (wheel_y != 0) {
        wheel_accum += wheel_y * 3.0f;
        int lines = (int)wheel_accum;
        if (lines != 0) {
            e->first_line -= lines;
            wheel_accum -= (float)lines;
            clamp_scroll(e, visible_lines);
        }
    }

    if (wheel_x != 0 && !preview_mode) {
        float visible_text_w = edit_area.width - gutter_w - 28;
        float max_x_scroll = compute_max_x_scroll(e, visible_text_w, char_w);
        if (max_x_scroll > 0) {
            e->x_scroll -= wheel_x * (char_w + TEXT_SPACING) * 3.0f;
            if (e->x_scroll < 0) e->x_scroll = 0;
            if (e->x_scroll > max_x_scroll) e->x_scroll = max_x_scroll;
        } else {
            e->x_scroll = 0;
        }
    }

    if (preview_mode) return;

    if (!mod) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            if (ch != ' ') {
                if (has_selection(e)) selection_delete(e);
                insert_char(e, ch);
                keep_cursor_visible = 1;
            }
        }
        if (key_action(KEY_SPACE)) {
            if (has_selection(e)) selection_delete(e);
            insert_char(e, ' ');
            keep_cursor_visible = 1;
        }
    }

    if (IsKeyPressed(KEY_ENTER)) {
        if (has_selection(e)) selection_delete(e);
        insert_newline(e);
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_TAB)) {
        if (has_selection(e)) selection_delete(e);
        insert_char(e, '\t');
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_BACKSPACE)) {
        if (has_selection(e)) selection_delete(e);
        else backspace(e);
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_DELETE)) {
        if (has_selection(e)) selection_delete(e);
        else delete_forward(e);
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_LEFT)) {
        if (shift) { if (!e->sel_active) { e->sel_anchor_row = e->row; e->sel_anchor_col = e->col; e->sel_active = 1; } }
        else clear_selection(e);
        move_left(e);
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_RIGHT)) {
        if (shift) { if (!e->sel_active) { e->sel_anchor_row = e->row; e->sel_anchor_col = e->col; e->sel_active = 1; } }
        else clear_selection(e);
        move_right(e);
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_UP)) {
        if (shift) { if (!e->sel_active) { e->sel_anchor_row = e->row; e->sel_anchor_col = e->col; e->sel_active = 1; } }
        else clear_selection(e);
        move_up(e);
        keep_cursor_visible = 1;
    }
    if (key_action(KEY_DOWN)) {
        if (shift) { if (!e->sel_active) { e->sel_anchor_row = e->row; e->sel_anchor_col = e->col; e->sel_active = 1; } }
        else clear_selection(e);
        move_down(e);
        keep_cursor_visible = 1;
    }
    if (IsKeyPressed(KEY_HOME)) {
        if (shift) { if (!e->sel_active) { e->sel_anchor_row = e->row; e->sel_anchor_col = e->col; e->sel_active = 1; } }
        else clear_selection(e);
        e->col = 0;
        e->preferred_col = e->col;
        keep_cursor_visible = 1;
    }
    if (IsKeyPressed(KEY_END)) {
        if (shift) { if (!e->sel_active) { e->sel_anchor_row = e->row; e->sel_anchor_col = e->col; e->sel_active = 1; } }
        else clear_selection(e);
        e->col = e->lines[e->row].len;
        e->preferred_col = e->col;
        keep_cursor_visible = 1;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), edit_area)) {
        Vector2 m = GetMousePosition();
        int row = e->first_line + (int)((m.y - edit_area.y - 8) / line_h);
        if (row < 0) row = 0;
        if (row >= e->count) row = e->count - 1;
        float text_x = m.x - edit_area.x - gutter_w - 12 + e->x_scroll;
        e->row = row;
        e->col = column_from_x(&e->lines[row], font, font_size, text_x);
        clamp_cursor(e);
        e->preferred_col = e->col;
        e->sel_active = 0;
        e->sel_anchor_row = e->row;
        e->sel_anchor_col = e->col;
        mouse_selecting = 1;
        keep_cursor_visible = 1;
    }

    if (mouse_selecting && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        int row = e->first_line + (int)((m.y - edit_area.y - 8) / line_h);
        if (row < 0) row = 0;
        if (row >= e->count) row = e->count - 1;
        float text_x = m.x - edit_area.x - gutter_w - 12 + e->x_scroll;
        e->row = row;
        e->col = column_from_x(&e->lines[row], font, font_size, text_x);
        clamp_cursor(e);
        e->preferred_col = e->col;
        if (e->row != e->sel_anchor_row || e->col != e->sel_anchor_col) e->sel_active = 1;
        keep_cursor_visible = 1;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) mouse_selecting = 0;

    if (keep_cursor_visible) ensure_cursor_visible(e, visible_lines, edit_area.width, gutter_w, char_w);

    float visible_text_w = edit_area.width - gutter_w - 28;
    float max_x_scroll = compute_max_x_scroll(e, visible_text_w, char_w);
    if (e->x_scroll > max_x_scroll) e->x_scroll = max_x_scroll;
    if (e->x_scroll < 0) e->x_scroll = 0;
}

static void draw_editor(Editor *e, Rectangle area, Font font, float font_size, Theme t) {
    float line_h = font_size + 8;
    float char_w = MeasureTextEx(font, "M", font_size, 0).x;
    float gutter_w = 58;
    int visible = (int)((area.height - 16) / line_h);
    clamp_scroll(e, visible);

    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    DrawRectangleRec(area, t.bg);

    int sel_r0 = 0, sel_c0 = 0, sel_r1 = 0, sel_c1 = 0;
    int selecting = has_selection(e);
    if (selecting) selection_range(e, &sel_r0, &sel_c0, &sel_r1, &sel_c1);

    static float adv[RENDER_LINE_CAP + 1];
    static Color colors[RENDER_LINE_CAP];
    float cursor_cx = 0;
    int cursor_found = 0;

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    for (int i = 0; i < visible && e->first_line + i < e->count; i++) {
        int row = e->first_line + i;
        float y = area.y + 8 + i * line_h;
        if (row == e->row) DrawRectangle((int)area.x, (int)y - 2, (int)area.width, (int)line_h, t.panel);

        Line *line = &e->lines[row];
        float text_left = area.x + gutter_w + 12 - e->x_scroll;
        int len = line->len;
        int fast = len <= RENDER_LINE_CAP;

        if (fast) {
            adv[0] = 0;
            for (int c = 0; c < len; c++) adv[c + 1] = adv[c] + glyph_advance(font, line->text[c], font_size);
            compute_line_colors(e, row, in_code_block(e, row), t, colors, len);
        }

        if (selecting && row >= sel_r0 && row <= sel_r1) {
            int from_col = (row == sel_r0) ? sel_c0 : 0;
            int to_col = (row == sel_r1) ? sel_c1 : len;
            if (from_col > len) from_col = len;
            if (to_col > len) to_col = len;
            float sx, ex;
            if (fast) {
                sx = text_left + adv[from_col];
                ex = text_left + adv[to_col];
            } else {
                sx = text_left;
                for (int c = 0; c < from_col; c++) sx += glyph_advance(font, line->text[c], font_size);
                ex = sx;
                for (int c = from_col; c < to_col; c++) ex += glyph_advance(font, line->text[c], font_size);
            }
            if (row != sel_r1 || to_col >= len) ex += glyph_advance(font, ' ', font_size);
            if (ex < sx + 4) ex = sx + 4;
            DrawRectangle((int)sx, (int)y - 2, (int)(ex - sx), (int)line_h, t.selection);
        }

        float x = text_left;
        for (int c = 0; c < len; c++) {
            if (line->text[c] != '\t') {
                char s[2] = {line->text[c], 0};
                Color col = fast ? colors[c] : syntax_color(e, row, c, t);
                DrawTextEx(font, s, (Vector2){x, y}, font_size, 0, col);
            }
            x += fast ? (adv[c + 1] - adv[c]) : glyph_advance(font, line->text[c], font_size);
        }

        if (row == e->row) {
            int cc = e->col;
            if (cc > len) cc = len;
            if (fast) {
                cursor_cx = text_left + adv[cc];
            } else {
                float cx = text_left;
                for (int c = 0; c < cc; c++) cx += glyph_advance(font, line->text[c], font_size);
                cursor_cx = cx;
            }
            cursor_found = 1;
        }
    }

    if (((int)(GetTime() * 2.0) % 2) == 0 && cursor_found) {
        float cy = area.y + 8 + (e->row - e->first_line) * line_h;
        if (cy >= area.y && cy < area.y + area.height) DrawRectangle((int)cursor_cx, (int)cy, 2, (int)(font_size + 4), t.accent);
    }

    /* Gutter overlay: drawn after the text pass so horizontally-scrolled text
       can never show through underneath the line numbers. */
    DrawRectangle((int)area.x, (int)area.y, (int)gutter_w, (int)area.height, t.bg);
    for (int i = 0; i < visible && e->first_line + i < e->count; i++) {
        int row = e->first_line + i;
        float y = area.y + 8 + i * line_h;
        if (row == e->row) DrawRectangle((int)area.x, (int)y - 2, (int)gutter_w, (int)line_h, t.panel);

        char num[32];
        snprintf(num, sizeof(num), "%d", row + 1);
        Vector2 ns = MeasureTextEx(font, num, font_size - 2, 0);
        DrawTextEx(font, num, (Vector2){area.x + gutter_w - ns.x - 12, y + 1}, font_size - 2, 0, t.muted);
    }
    DrawLine((int)(area.x + gutter_w), (int)area.y, (int)(area.x + gutter_w), (int)(area.y + area.height), t.border);

    float visible_text_w = area.width - gutter_w - 28;
    float max_x_scroll = compute_max_x_scroll(e, visible_text_w, char_w);
    if (max_x_scroll > 0) {
        float track_x = area.x + gutter_w + 4;
        float track_w = area.width - gutter_w - 8;
        float track_y = area.y + area.height - 8;
        float thumb_w = track_w * (visible_text_w / (visible_text_w + max_x_scroll));
        if (thumb_w < 30) thumb_w = 30;
        if (thumb_w > track_w) thumb_w = track_w;
        float thumb_x = track_x + (track_w - thumb_w) * (e->x_scroll / max_x_scroll);
        DrawRectangleRounded((Rectangle){track_x, track_y, track_w, 4}, 0.5f, 4, t.panel2);
        DrawRectangleRounded((Rectangle){thumb_x, track_y, thumb_w, 4}, 0.5f, 4, t.muted);
    }
    EndScissorMode();
}

static const char *find_close(const char *s, const char *delim) {
    const char *p = strstr(s, delim);
    return p && p > s ? p : NULL;
}

static void draw_preview_chars(Font font, const char *text, int len, DrawPos *pos, float left, float right,
                               float size, Color color, Theme t, int bold, int italic, int strike, int code,
                               const char *link_url) {
    float line_h = size + 8;
    for (int i = 0; i < len; i++) {
        char ch[2] = {text[i], 0};
        float cw = glyph_advance(font, text[i], size);
        if (pos->x + cw > right && pos->x > left) {
            pos->x = left;
            pos->y += line_h;
        }

        if (code) {
            Rectangle bg = {pos->x - 2, pos->y - 2, cw + 3, size + 6};
            DrawRectangleRounded(bg, 0.08f, 4, t.panel);
        }

        Vector2 p = {pos->x + (italic ? 0.8f : 0.0f), pos->y};
        DrawTextEx(font, ch, p, size, TEXT_SPACING, color);
        if (bold) DrawTextEx(font, ch, (Vector2){p.x + 0.8f, p.y}, size, TEXT_SPACING, color);
        if (strike) DrawLine((int)pos->x, (int)(pos->y + size * 0.55f), (int)(pos->x + cw), (int)(pos->y + size * 0.55f), color);
        if (link_url) {
            Rectangle hit = {pos->x, pos->y, cw, size + 6};
            DrawLine((int)pos->x, (int)(pos->y + size + 2), (int)(pos->x + cw), (int)(pos->y + size + 2), color);
            if (CheckCollisionPointRec(GetMousePosition(), hit)) {
                SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
                if (!preview_link_opened && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    OpenURL(link_url);
                    preview_link_opened = 1;
                    set_status("Opened link");
                }
            }
        }

        pos->x += cw;
    }
}

static void draw_inline_markdown_range(Font font, const char *text, const char *limit, DrawPos *pos, float left, float right,
                                       float size, Color color, Theme t, int bold, int italic) {
    const char *p = text;

    while (*p && p < limit) {
        if (strncmp(p, "**", 2) == 0 || strncmp(p, "__", 2) == 0) {
            char delim[3] = {p[0], p[1], 0};
            const char *end = find_close(p + 2, delim);
            if (end && end < limit) {
                draw_inline_markdown_range(font, p + 2, end, pos, left, right, size, color, t, 1, italic);
                p = end + 2;
                continue;
            }
        }

        if ((*p == '*' || *p == '_') && p[1] != p[0]) {
            char delim[2] = {p[0], 0};
            const char *end = find_close(p + 1, delim);
            if (end && end < limit) {
                draw_inline_markdown_range(font, p + 1, end, pos, left, right, size, color, t, bold, 1);
                p = end + 1;
                continue;
            }
        }

        if (strncmp(p, "~~", 2) == 0) {
            const char *end = find_close(p + 2, "~~");
            if (end && end < limit) {
                draw_preview_chars(font, p + 2, (int)(end - (p + 2)), pos, left, right, size, color, t, bold, italic, 1, 0, NULL);
                p = end + 2;
                continue;
            }
        }

        if (*p == '`') {
            const char *end = strchr(p + 1, '`');
            if (end && end < limit) {
                draw_preview_chars(font, p + 1, (int)(end - (p + 1)), pos, left, right, size, t.text, t, 0, 0, 0, 1, NULL);
                p = end + 1;
                continue;
            }
        }

        if (*p == '[') {
            const char *label_end = strchr(p + 1, ']');
            if (label_end && label_end[1] == '(') {
                const char *url_end = strchr(label_end + 2, ')');
                if (url_end && url_end < limit) {
                    char url[1024];
                    int url_len = (int)(url_end - (label_end + 2));
                    if (url_len > (int)sizeof(url) - 1) url_len = (int)sizeof(url) - 1;
                    memcpy(url, label_end + 2, (size_t)url_len);
                    url[url_len] = '\0';
                    draw_preview_chars(font, p + 1, (int)(label_end - (p + 1)), pos, left, right, size, t.link, t, bold, italic, 0, 0, url);
                    p = url_end + 1;
                    continue;
                }
            }
        }

        draw_preview_chars(font, p, 1, pos, left, right, size, color, t, bold, italic, 0, 0, NULL);
        p++;
    }
}

static void draw_inline_markdown(Font font, const char *text, DrawPos *pos, float left, float right,
                                 float size, Color color, Theme t) {
    draw_inline_markdown_range(font, text, text + strlen(text), pos, left, right, size, color, t, 0, 0);
}

static void draw_markdown_preview(Editor *e, Rectangle area, Font font, float font_size, Theme t) {
    float y = area.y + 18;
    float x = area.x + 32;
    float max_w = area.width - 64;
    int in_code = 0;

    preview_link_opened = 0;
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    DrawRectangleRec(area, t.bg);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);

    for (int row = e->first_line; row < e->count && y < area.y + area.height; row++) {
        char buffer[4096];
        snprintf(buffer, sizeof(buffer), "%s", e->lines[row].text);

        if (line_is_fence(buffer)) {
            in_code = !in_code;
            continue;
        }

        if (buffer[0] == '\0') {
            y += font_size * 0.8f;
            continue;
        }

        if (line_is_table_row(buffer) && row + 1 < e->count && line_is_table_separator(e->lines[row + 1].text)) {
            static char cell_text[MAX_TABLE_ROWS][MAX_TABLE_COLS][MAX_TABLE_CELL];
            static int cell_counts[MAX_TABLE_ROWS];
            int table_rows = 0;
            int cols = 0;

            cell_counts[table_rows] = parse_table_row(buffer, cell_text[table_rows], MAX_TABLE_COLS);
            if (cell_counts[table_rows] > cols) cols = cell_counts[table_rows];
            table_rows++;

            int r = row + 2;
            while (r < e->count && table_rows < MAX_TABLE_ROWS && line_is_table_row(e->lines[r].text) &&
                   !line_is_table_separator(e->lines[r].text)) {
                cell_counts[table_rows] = parse_table_row(e->lines[r].text, cell_text[table_rows], MAX_TABLE_COLS);
                if (cell_counts[table_rows] > cols) cols = cell_counts[table_rows];
                table_rows++;
                r++;
            }
            if (cols > MAX_TABLE_COLS) cols = MAX_TABLE_COLS;
            if (cols < 1) cols = 1;

            float col_w[MAX_TABLE_COLS];
            for (int c = 0; c < cols; c++) col_w[c] = 24.0f;
            for (int rr = 0; rr < table_rows; rr++) {
                for (int c = 0; c < cell_counts[rr] && c < cols; c++) {
                    Vector2 ts = MeasureTextEx(font, cell_text[rr][c], font_size - 1, TEXT_SPACING);
                    if (ts.x + 16.0f > col_w[c]) col_w[c] = ts.x + 16.0f;
                }
            }
            float total_w = 0;
            for (int c = 0; c < cols; c++) total_w += col_w[c];
            if (total_w > max_w) {
                float scale = max_w / total_w;
                for (int c = 0; c < cols; c++) col_w[c] *= scale;
                total_w = max_w;
            }

            float row_h = font_size + 12.0f;
            float table_top = y;
            float table_y = y;

            DrawRectangle((int)x, (int)table_y, (int)total_w, (int)row_h, t.panel2);
            float cx = x;
            for (int c = 0; c < cols; c++) {
                const char *txt = c < cell_counts[0] ? cell_text[0][c] : "";
                DrawTextEx(font, txt, (Vector2){cx + 8, table_y + 6}, font_size - 1, TEXT_SPACING, t.heading);
                cx += col_w[c];
            }
            table_y += row_h;

            for (int rr = 1; rr < table_rows; rr++) {
                cx = x;
                for (int c = 0; c < cols; c++) {
                    const char *txt = c < cell_counts[rr] ? cell_text[rr][c] : "";
                    DrawPos cpos = {cx + 8, table_y + 6};
                    draw_inline_markdown(font, txt, &cpos, cx + 8, cx + col_w[c] - 8, font_size - 1, t.text, t);
                    cx += col_w[c];
                }
                table_y += row_h;
            }

            for (float ly = table_top; ly <= table_y + 0.5f; ly += row_h) {
                DrawLine((int)x, (int)ly, (int)(x + total_w), (int)ly, t.border);
            }
            cx = x;
            for (int c = 0; c <= cols; c++) {
                DrawLine((int)cx, (int)table_top, (int)cx, (int)table_y, t.border);
                if (c < cols) cx += col_w[c];
            }

            y = table_y + 9;
            row = r - 1;
            continue;
        }

        int heading = 0;
        int quote = 0;
        int bullet = 0;
        char *text = trim_markdown_prefix(buffer, &heading, &quote, &bullet);
        float size = font_size;
        float left = x;
        Color color = t.text;

        if (heading > 0) {
            size = font_size + (float)(7 - heading) * 1.5f;
            if (size > font_size + 8.0f) size = font_size + 8.0f;
            color = t.accent;
            y += font_size * 0.3f;
        } else if (quote) {
            DrawRectangle((int)x, (int)y, 3, (int)(font_size + 8), t.border);
            left += 16;
            color = t.muted;
        } else if (bullet) {
            DrawCircle((int)(x + 5), (int)(y + font_size * 0.58f), 3.0f, t.muted);
            left += 20;
        } else if (in_code) {
            Rectangle code_bg = {x - 8, y - 4, max_w + 16, font_size + 10};
            DrawRectangleRounded(code_bg, 0.05f, 4, t.panel);
            color = t.text;
        }

        DrawPos pos = {left, y};
        draw_inline_markdown(font, text, &pos, left, area.x + area.width - 32, size, color, t);
        y = pos.y + size + 9;
    }

    EndScissorMode();
}

static void draw_prompt(Rectangle screen, Font font, float size, Theme t) {
    if (prompt_mode == PROMPT_NONE) return;
    DrawRectangle(0, 0, (int)screen.width, (int)screen.height, (Color){0, 0, 0, 120});
    Rectangle box = {screen.width * 0.5f - 360, screen.height * 0.5f - 56, 720, 112};
    if (box.x < 20) {
        box.x = 20;
        box.width = screen.width - 40;
    }
    DrawRectangleRounded(box, 0.08f, 8, t.panel);
    DrawRectangleRoundedLinesEx(box, 0.08f, 8, 1, t.border);
    const char *title = prompt_mode == PROMPT_OPEN ? "Open text or Markdown file" : "Save as .md, .markdown, or .txt";
    DrawTextEx(font, title, (Vector2){box.x + 18, box.y + 14}, size, 0, t.text);
    Rectangle input = {box.x + 18, box.y + 52, box.width - 36, 38};
    DrawRectangleRounded(input, 0.12f, 8, t.bg);
    DrawRectangleRoundedLinesEx(input, 0.12f, 8, 1, t.accent);
    BeginScissorMode((int)input.x + 10, (int)input.y, (int)input.width - 20, (int)input.height);
    DrawTextEx(font, prompt_text, (Vector2){input.x + 10, input.y + 9}, size, 0, t.text);
    Vector2 ts = MeasureTextEx(font, prompt_text, size, 0);
    if (((int)(GetTime() * 2.0) % 2) == 0) DrawRectangle((int)(input.x + 10 + ts.x), (int)(input.y + 8), 2, 22, t.accent);
    EndScissorMode();
}

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static char g_ae_pending_path[MAX_PATH_EDIT];
static int g_ae_pending = 0;

static OSErr handle_open_documents_ae(const AppleEvent *event, AppleEvent *reply, long refcon) {
    (void)reply;
    (void)refcon;

    AEDescList doc_list;
    OSErr err = AEGetParamDesc(event, keyDirectObject, typeAEList, &doc_list);
    if (err != noErr) return err;

    long count = 0;
    AECountItems(&doc_list, &count);
    for (long i = 1; i <= count; i++) {
        AEKeyword keyword;
        DescType actual_type;
        Size actual_size;
        FSRef fs_ref;
        err = AEGetNthPtr(&doc_list, i, typeFSRef, &keyword, &actual_type, &fs_ref, sizeof(fs_ref), &actual_size);
        if (err == noErr) {
            UInt8 path[MAX_PATH_EDIT];
            if (FSRefMakePath(&fs_ref, path, sizeof(path)) == noErr) {
                snprintf(g_ae_pending_path, sizeof(g_ae_pending_path), "%s", (char *)path);
                g_ae_pending = 1; /* last document in the list wins if multiple are sent at once */
            }
        }
    }
    AEDisposeDesc(&doc_list);
    return noErr;
}

static void install_mac_open_document_handler(void) {
    AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, NewAEEventHandlerUPP(handle_open_documents_ae), 0, 0);
}
#pragma clang diagnostic pop
#endif

int main(int argc, char **argv) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

#if defined(__APPLE__)
    install_mac_open_document_handler();
#endif

    InitWindow(1120, 760, APP_TITLE);
    SetTargetFPS(60);

    int loaded_font_size = (int)(editor_font_size + 0.5f);
    int custom_font = 0;
    Font font = load_app_font(loaded_font_size, &custom_font);

    Editor editor;
    editor_init(&editor);
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue; /* e.g. -psn_... injected by Finder/Launch Services */
        editor_load(&editor, argv[i]);
        break;
    }

    while (!WindowShouldClose()) {
#if defined(__APPLE__)
        if (g_ae_pending) {
            g_ae_pending = 0;
            editor_load(&editor, g_ae_pending_path);
        }
#endif
        int wanted_font_size = (int)(editor_font_size + 0.5f);
        if (wanted_font_size != loaded_font_size) {
            if (custom_font) UnloadFont(font);
            font = load_app_font(wanted_font_size, &custom_font);
            loaded_font_size = wanted_font_size;
        }

        Theme t = get_theme();
        float w = (float)GetScreenWidth();
        float h = (float)GetScreenHeight();
        float font_size = editor_font_size;
        float top_h = 56.0f;
        float status_h = 28.0f;
        Rectangle edit_area = {0, top_h, w, h - top_h - status_h};

        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count > 0) editor_load(&editor, dropped.paths[0]);
            UnloadDroppedFiles(dropped);
        }

        if (prompt_mode != PROMPT_NONE) handle_prompt(&editor);
        else handle_editor_input(&editor, edit_area, font, font_size, 58, MeasureTextEx(font, "M", font_size, 0).x, font_size + 8);

        BeginDrawing();
        ClearBackground(t.bg);

        DrawRectangle(0, 0, (int)w, (int)top_h, t.panel);
        DrawLine(0, (int)top_h - 1, (int)w, (int)top_h - 1, t.border);

        float x = 12;
        if (button((Rectangle){x, 12, 72, 32}, "New", font, 16, t)) editor_new(&editor);
        x += 82;
        if (button((Rectangle){x, 12, 76, 32}, "Open", font, 16, t)) open_file_dialog(&editor);
        x += 86;
        if (button((Rectangle){x, 12, 72, 32}, "Save", font, 16, t)) {
            if (!editor_save(&editor)) save_file_dialog(&editor);
        }
        x += 82;
        if (button((Rectangle){x, 12, 92, 32}, "Save As", font, 16, t)) save_file_dialog(&editor);
        x += 104;
        if (button((Rectangle){x, 12, 132, 32}, preview_mode ? "Markdown Text" : "Preview", font, 16, t)) {
            preview_mode = !preview_mode;
            set_status(preview_mode ? "Showing Markdown preview" : "Showing Markdown text");
        }
        x += 144;
        if (theme_toggle((Rectangle){x, 12, 88, 32}, font, 14, t)) {
            dark_mode = !dark_mode;
            set_status(dark_mode ? "Dark mode" : "Light mode");
        }
        x += 100;
        if (button((Rectangle){x, 12, 34, 32}, "-", font, 18, t)) zoom_text(-2.0f);
        x += 42;
        char zoom_label[32];
        snprintf(zoom_label, sizeof(zoom_label), "%.0f", editor_font_size);
        if (button((Rectangle){x, 12, 46, 32}, zoom_label, font, 15, t)) editor_font_size = 18.0f;
        x += 54;
        if (button((Rectangle){x, 12, 34, 32}, "+", font, 18, t)) zoom_text(2.0f);

        const char *name = editor.path[0] ? base_name(editor.path) : "Untitled";
        char title[512];
        snprintf(title, sizeof(title), "%s%s", name, editor.dirty ? " *" : "");
        Vector2 title_size = MeasureTextEx(font, title, 17, BUTTON_TEXT_SPACING);
        DrawTextEx(font, title, (Vector2){w - title_size.x - 18, 18}, 17, BUTTON_TEXT_SPACING, editor.dirty ? t.accent : t.muted);

        if (preview_mode && editor.markdown) draw_markdown_preview(&editor, edit_area, font, font_size, t);
        else draw_editor(&editor, edit_area, font, font_size, t);

        DrawRectangle(0, (int)(h - status_h), (int)w, (int)status_h, t.panel);
        DrawLine(0, (int)(h - status_h), (int)w, (int)(h - status_h), t.border);
        if (status_timer > 0) status_timer -= GetFrameTime();
        const char *left = status_timer > 0 ? status_msg : "Cmd+O open  Cmd+S save  Cmd+L theme  Cmd+P preview/text  Cmd +/- zoom";
        DrawTextEx(font, left, (Vector2){12, h - 21}, 14, STATUS_TEXT_SPACING, t.muted);
        char pos[128];
        snprintf(pos, sizeof(pos), "%s  %.0fpt  Ln %d, Col %d",
                 preview_mode && editor.markdown ? "Preview" : (editor.markdown ? "Markdown text" : "Text"),
                 editor_font_size, editor.row + 1, editor.col + 1);
        Vector2 ps = MeasureTextEx(font, pos, 14, STATUS_TEXT_SPACING);
        DrawTextEx(font, pos, (Vector2){w - ps.x - 12, h - 21}, 14, STATUS_TEXT_SPACING, t.muted);

        draw_prompt((Rectangle){0, 0, w, h}, font, 18, t);
        EndDrawing();
    }

    editor_free(&editor);
    if (custom_font) UnloadFont(font);
    CloseWindow();
    return 0;
}
