/* ═══════════════════════════════════════════════════════════════════════════
 * Implemented features:
 *  [x] Selection of individual files and directories, with visual highlight
 *      and keyboard navigation (arrow keys, Home, End, PgUp, PgDn).
 *  [x] "Open" action on selected entry (Enter): cd into directories,
 *      download files, handle the special "move up" entry.
 *  [x] Ctrl+D shortcut – download the currently selected entry.
 *  [x] Ctrl+U shortcut – upload a local file to the current directory
 *      (prompts for a local file path).
 *  [x] Delete key – delete the currently selected entry with a
 *      confirmation prompt.
 *
 * Compile (send-file-socket.c is #included directly, do NOT pass it
 * as a separate translation unit):
 *
 *   gcc main2.c -o main -Wall -Wextra -lcrypt -lncurses
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "./send-file-socket.c"

#include <locale.h>
#include <ncurses.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ── DEBUG ─────────────────────────────────────────────────────────────── */
#define DEBUG 0

/* ── Network target ────────────────────────────────────────────────────── */
#define SERVER_ADDRESS /**/ "192.168.1.102" /** / "10.0.0.1" /**/
#define SERVER_PORT    "9001"

/* ── Layout constants ──────────────────────────────────────────────────── */
#define USERNAME_ROW   0
#define PASSWORD_ROW   1
#define LABEL_COL      0
#define INPUT_COL      10   /* "Password: " is 10 chars wide */

/* ── Buffer sizes ──────────────────────────────────────────────────────── */
#define INPUT_BUF_MAX  1024

/* ═══════════════════════════════════════════════════════════════════════════
 * struct input_state
 *
 * A single, reusable input slot.  Only one field is "live" at a time;
 * when focus moves away, flush_input_to() copies the buffer into a plain
 * char[] and resets this struct for the next field.
 *
 *   buffer          – Null-terminated character array holding the typed text.
 *   length          – Number of characters currently in the buffer
 *                     (excludes the null terminator).
 *   max_length      – Hard capacity of `buffer`, including the null
 *                     terminator.  Characters are rejected once
 *                     length >= max_length - 1.
 *   cursor_position – Logical index within `buffer` at which the next
 *                     keystroke will be inserted (0 … length, inclusive).
 * ═══════════════════════════════════════════════════════════════════════════ */
struct input_state
{
    char buffer[INPUT_BUF_MAX];
    int  length;
    int  max_length;
    int  cursor_position;
};

/* ── Single global input slot ───────────────────────────────────────────── */
static struct input_state g_input = {
    .buffer          = {0},
    .length          = 0,
    .max_length      = INPUT_BUF_MAX,
    .cursor_position = 0
};


enum SPRITES
{
    MOVE_UP,
    FOLDER,
    EMPTY_FOLDER,
    NEW_FOLDER,
    GENERIC_FILE,
    TEXT_FILE,
    BIN_FILE,
    CODE_FILE,
    HTML_FILE,
    LINK_FILE,
    VIDEO_FILE,
    AUDIO_FILE,
    IMAGE_FILE,
    ZIP_FILE,
    NEW_FILE,
    SPRITE_COUNT
};

#define SPRITE_HEIGHT 4
#define SPRITE_WIDTH  8
#define TILE_WIDTH    24
#define TILE_HEIGHT   6
#define LIST_TOP_ROW  6

typedef struct
{
    const char *rows[SPRITE_HEIGHT];
} Sprite;

static const Sprite SPRITE_TABLE[SPRITE_COUNT] =
{
    [MOVE_UP] = { .rows = {
        "  .^.   ",
        " /   \\  ",
        "'─┐ ┌─' ",
        "  └─┘   "
    }},
    [FOLDER] = { .rows = {
        "┌──┐___.",
        "│      │",
        "│      │",
        "└──────┘"
    }},
    [EMPTY_FOLDER] = { .rows = {
        "┌──┐___.",
        "│  └───┤",
        "│      │",
        "└──────┘"
    }},
    [NEW_FOLDER] = { .rows = {
        "  ┌─┐   ",
        "┌─┘ └─┐ ",
        "└─┐ ┌─┘ ",
        "  └─┘   "
    }},
    [GENERIC_FILE] = { .rows = {
        "┌────.  ",
        "│     \\ ",
        "│     │ ",
        "└─────┘ "
    }},
    [TEXT_FILE] = { .rows = {
        "┌────.  ",
        "│ --- \\ ",
        "│ --- │ ",
        "└─────┘ "
    }},
    [BIN_FILE] = { .rows = {
        "┌────.  ",
        "│ ... \\ ",
        "│ ... │ ",
        "└─────┘ "
    }},
    [CODE_FILE] = { .rows = {
        "┌────.  ",
        "│     \\ ",
        "│ py  │ ",
        "└─────┘ "
    }},
    [HTML_FILE] = { .rows = {
        "┌────.  ",
        "│     \\ ",
        "│ </> │ ",
        "└─────┘ "
    }},
    [LINK_FILE] = { .rows = {
        "┌────.  ",
        "│     \\ ",
        "│ ./  │ ",
        "└─────┘ "
    }},
    [VIDEO_FILE] = { .rows = {
        "┌────.  ",
        "│ |\\  \\ ",
        "│ |/  │ ",
        "└─────┘ "
    }},
    [AUDIO_FILE] = { .rows = {
        "┌────.  ",
        "│  ┌~ \\ ",
        "│ O┘  │ ",
        "└─────┘ "
    }},
    [IMAGE_FILE] = { .rows = {
        "┌────.  ",
        "│ /\\ o\\ ",
        "│/  \\/│ ",
        "└─────┘ "
    }},
    [ZIP_FILE] = { .rows = {
        "┌────.  ",
        "│.  ┴ \\ ",
        "│.  ┴ │ ",
        "└───┴─┘ "
    }},
    [NEW_FILE] = { .rows = {
        "  ┌─┐   ",
        "┌─┘ └─┐ ",
        "└─┐ ┌─┘ ",
        "  └─┘   "
    }},
};

static const Sprite *get_sprite(enum SPRITES id)
{
    if (id < 0 || id >= SPRITE_COUNT) return NULL;
    return &SPRITE_TABLE[id];
}

static void draw_sprite(int row, int col, enum SPRITES id, const char *filename)
{
    const Sprite *sprite = get_sprite(id);
    if (!sprite)
        return;

    for (int i = 0; i < SPRITE_HEIGHT; i++)
        mvaddstr(row + i, col, sprite->rows[i]);

    /* For CODE_FILE, overlay the actual file extension */
    if (id == CODE_FILE && filename)
    {
        const char *ext = strrchr(filename, '.');
        if (ext && ext != filename)
        {
            ext++; /* Skip the dot */
            char ext_display[4] = {0};
            strncpy(ext_display, ext, 3);

            /* Convert to lowercase for display */
            for (int i = 0; ext_display[i]; i++)
                ext_display[i] = tolower((unsigned char)ext_display[i]);

            /* Draw at row+2 (where "py" is), col+2 (where "py" starts) */
            mvprintw(row + 2, col + 2, "%-3s", ext_display);
        }
    }
}

static enum SPRITES classify_sprite(const char *name, int is_dir)
{
    const char *ext;

    if (!name || !name[0])
        return GENERIC_FILE;

    if (strcmp(name, "..") == 0)
        return MOVE_UP;

    if (strcmp(name, ".") == 0)
        return MOVE_UP;

    if (is_dir)
        return FOLDER;

    ext = strrchr(name, '.');
    if (!ext || ext == name)
        return GENERIC_FILE;

    if (strcasecmp(ext, ".txt") == 0 ||
        strcasecmp(ext, ".md") == 0 ||
        strcasecmp(ext, ".log") == 0)
        return TEXT_FILE;

    if (strcasecmp(ext, ".c") == 0 ||
        strcasecmp(ext, ".h") == 0 ||
        strcasecmp(ext, ".cpp") == 0 ||
        strcasecmp(ext, ".hpp") == 0 ||
        strcasecmp(ext, ".py") == 0 ||
        strcasecmp(ext, ".js") == 0 ||
        strcasecmp(ext, ".ts") == 0 ||
        strcasecmp(ext, ".java") == 0)
        return CODE_FILE;

    if (strcasecmp(ext, ".html") == 0 ||
        strcasecmp(ext, ".htm") == 0)
        return HTML_FILE;

    if (strcasecmp(ext, ".zip") == 0 ||
        strcasecmp(ext, ".tar") == 0 ||
        strcasecmp(ext, ".gz") == 0 ||
        strcasecmp(ext, ".7z") == 0 ||
        strcasecmp(ext, ".rar") == 0)
        return ZIP_FILE;

    if (strcasecmp(ext, ".png") == 0 ||
        strcasecmp(ext, ".jpg") == 0 ||
        strcasecmp(ext, ".jpeg") == 0 ||
        strcasecmp(ext, ".gif") == 0 ||
        strcasecmp(ext, ".webp") == 0 ||
        strcasecmp(ext, ".svg") == 0)
        return IMAGE_FILE;

    if (strcasecmp(ext, ".mp4") == 0 ||
        strcasecmp(ext, ".mkv") == 0 ||
        strcasecmp(ext, ".avi") == 0 ||
        strcasecmp(ext, ".mov") == 0)
        return VIDEO_FILE;

    if (strcasecmp(ext, ".mp3") == 0 ||
        strcasecmp(ext, ".wav") == 0 ||
        strcasecmp(ext, ".ogg") == 0 ||
        strcasecmp(ext, ".flac") == 0)
        return AUDIO_FILE;

    if (strcasecmp(ext, ".lnk") == 0 ||
        strcasecmp(ext, ".url") == 0)
        return LINK_FILE;

    if (strcasecmp(ext, ".bin") == 0 ||
        strcasecmp(ext, ".dat") == 0)
        return BIN_FILE;

    return GENERIC_FILE;
}

/**
 * path_join - Safely join a directory path and a name into out_buf.
 *
 * Writes "dir/name" (or just "dir" when name is empty) into out_buf,
 * clamping the result to out_size - 1 bytes.  Always NUL-terminates.
 *
 * @param out_buf   Destination buffer.
 * @param out_size  Capacity of out_buf in bytes.
 * @param dir       Directory component (may contain trailing slash, handled).
 * @param name      Name component to append.
 */
static void path_join(char *out_buf, size_t out_size,
                      const char *dir, const char *name)
{
    size_t dir_len  = strlen(dir);
    /* Strip trailing slash from dir, unless it's exactly "/" */
    while (dir_len > 1 && dir[dir_len - 1] == '/')
        dir_len--;

    if (!name || !name[0]) {
        size_t copy = (dir_len < out_size - 1) ? dir_len : out_size - 1;
        memcpy(out_buf, dir, copy);
        out_buf[copy] = '\0';
        return;
    }

    size_t name_len = strlen(name);
    size_t total    = dir_len + 1 + name_len;   /* dir + '/' + name */

    if (total >= out_size)
        total = out_size - 1;

    size_t dpart = (dir_len < total) ? dir_len : total;
    memcpy(out_buf, dir, dpart);

    size_t pos = dpart;
    if (pos < total) { out_buf[pos++] = '/'; }

    size_t npart = total - pos;
    memcpy(out_buf + pos, name, npart);
    out_buf[total] = '\0';
}

static int parse_listing_entries(const char *listing, char ***entries_out, int **types_out)
{
    char  *copy;
    char  *tok;
    char **entries = NULL;
    int   *types   = NULL;
    int    count   = 0;

    *entries_out = NULL;
    *types_out   = NULL;
    if (!listing || !listing[0])
        return 0;

    copy = strdup(listing);
    if (!copy)
        return 0;

    tok = strtok(copy, "\n");
    while (tok)
    {
        char **etmp = realloc(entries, (size_t)(count + 1) * sizeof(*entries));
        int   *ttmp = realloc(types,   (size_t)(count + 1) * sizeof(*types));
        if (!etmp || !ttmp)
            break;

        entries = etmp;
        types   = ttmp;

        /* Strip the "d:" / "f:" prefix written by list_directory_sock(). */
        int is_dir = 0;
        const char *name = tok;
        if (tok[0] == 'd' && tok[1] == ':') { is_dir = 1; name = tok + 2; }
        else if (tok[0] == 'f' && tok[1] == ':') {          name = tok + 2; }

        entries[count] = strdup(name);
        if (!entries[count])
            break;

        types[count] = is_dir;
        count++;
        tok = strtok(NULL, "\n");
    }

    free(copy);
    *entries_out = entries;
    *types_out   = types;
    return count;
}

static void free_listing_entries(char **entries, int *types, int count)
{
    for (int i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
    free(types);
}

/* ── Stored credential strings (plain buffers, not input_states) ─────────── */
static char username[INPUT_BUF_MAX] = {0};
static char password[INPUT_BUF_MAX] = {0};

/* ── Helpers ────────────────────────────────────────────────────────────── */

/**
 * flush_input_to – Copies g_input.buffer into `dest` then resets g_input.
 *
 * Call this whenever focus leaves a field so that the typed value is
 * preserved in a plain string while the shared slot is ready for the
 * next field.
 */
static void flush_input_to(char *dest)
{
    memcpy(dest, g_input.buffer, (size_t)(g_input.length + 1)); /* +1 for '\0' */
    memset(&g_input, 0, sizeof g_input);
    g_input.max_length = INPUT_BUF_MAX;
}

/**
 * load_input_from – Loads a previously stored string back into g_input.
 *
 * Call this when returning focus to a field that was already filled in,
 * so the user can continue editing where they left off.
 */
static void load_input_from(const char *src)
{
    int len = (int)strlen(src);
    memcpy(g_input.buffer, src, (size_t)(len + 1));
    g_input.length          = len;
    g_input.cursor_position = len;   /* Place cursor at the end. */
    g_input.max_length      = INPUT_BUF_MAX;
}

/**
 * redraw_field – Repaints a single input field in place.
 *
 * Moves to (row, INPUT_COL), clears to end-of-line, then reprints every
 * character in g_input (or '*' when hide_input is true).  Finally
 * positions the screen cursor at the logical cursor_position.
 */
static void redraw_field(int row, bool hide_input)
{
    move(row, INPUT_COL);
    clrtoeol();

    for (int i = 0; i < g_input.length; i++)
        addch(hide_input ? '*' : (unsigned char)g_input.buffer[i]);

    move(row, INPUT_COL + g_input.cursor_position);
}

/**
 * handle_input – Applies one keypress to g_input.
 *
 * Does NOT touch the screen; the caller is responsible for calling
 * redraw_field() afterwards so the display stays consistent with the
 * buffer.
 *
 * Supported keys:
 *   KEY_BACKSPACE / 127  – delete the character to the left of the cursor.
 *   KEY_LEFT             – move cursor one step left (clamped at 0).
 *   KEY_RIGHT            – move cursor one step right (clamped at length).
 *   32–126 (printable)   – insert character at cursor, shifting right.
 *   All other values     – silently ignored.
 *
 * Returns true if the buffer was modified (so the caller can decide
 * whether a redraw is necessary).
 */
static bool handle_input(int ch)
{
    if (ch == KEY_BACKSPACE || ch == 127)
    {
        if (g_input.cursor_position == 0)
            return false;

        int del = g_input.cursor_position - 1;
        memmove(&g_input.buffer[del],
                &g_input.buffer[del + 1],
                (size_t)(g_input.length - del)); /* includes '\0' */

        g_input.cursor_position--;
        g_input.length--;
        return true;
    }

    if (ch == KEY_LEFT)
    {
        if (g_input.cursor_position > 0) { g_input.cursor_position--; return true; }
        return false;
    }

    if (ch == KEY_RIGHT)
    {
        if (g_input.cursor_position < g_input.length) { g_input.cursor_position++; return true; }
        return false;
    }

    if ((ch >= 32 && ch < 127) || (ch >= 128 && ch <= 255))
    {
        if (g_input.length >= g_input.max_length - 1)
            return false;

        memmove(&g_input.buffer[g_input.cursor_position + 1],
                &g_input.buffer[g_input.cursor_position],
                (size_t)(g_input.length - g_input.cursor_position + 1));

        g_input.buffer[g_input.cursor_position] = (char)ch;
        g_input.cursor_position++;
        g_input.length++;
        return true;
    }

    return false;
}

/* ── Login screen ───────────────────────────────────────────────────────── */

static void show_login_labels(void)
{
    mvprintw(USERNAME_ROW, LABEL_COL, "Username: ");
    mvprintw(PASSWORD_ROW, LABEL_COL, "Password: ");
}

/* ── Explorer screen ────────────────────────────────────────────────────── */

static void show_explorer_header(char *ip_address, char *uname, char *current_directory,
                                 int page, int max_page, char *debug_message)
{
    int width = getmaxx(stdscr);

    move(0, 0);
    for (int i = 0; i < width; ++i) { addch(' '); addch('-'); }

    char *header_text = malloc(width + 1);
    snprintf(header_text, width + 1, " Connected to: %s || Username: %s ", ip_address, uname);
    int info_length = (int)strlen(header_text);
    int padding     = (width - info_length) / 2;

    mvprintw(0, padding, "%s", header_text);

    move(1, 0);
    for (int i = 0; i < width; ++i) addch('=');

    mvprintw(2, 0, "%s", current_directory);

    snprintf(header_text, width + 1, "page %d/%d", page, max_page);
    info_length = (int)strlen(header_text);
    padding     = width - info_length;
    mvprintw(2, padding, "%s", header_text);

    move(3, 0);
    for (int i = 0; i < width; ++i) addch('-');

    if (DEBUG)
        mvprintw(4, 0, "DEBUG: %s", debug_message);

    free(header_text);
}



/*
 * Special sentinel names used for the synthetic MOVE_UP, NEW_FOLDER and
 * NEW_FILE slots that are injected around the real directory entries.
 * classify_sprite() already handles ".." → MOVE_UP; we add two more.
 */
#define SENTINEL_NEW_FOLDER "[ new folder ]"
#define SENTINEL_NEW_FILE   "[ new file ]"

/*
 * entry_type values used internally in show_explorer_listing.
 * Mirrors the LIST_TYPE_* wire constants but adds sentinel types.
 */
#define ETYPE_DIR        1
#define ETYPE_FILE       0
#define ETYPE_MOVE_UP   -1
#define ETYPE_NEW_FOLDER -2
#define ETYPE_NEW_FILE   -3

static int show_explorer_listing(char *listing, int *current_page_zero_based,
                                 int selected_index, int *total_entries_out)
{
    /* ── Parse the raw listing from the server ───────────────────────── */
    char **raw_entries = NULL;
    int   *raw_types   = NULL;
    int    raw_count   = parse_listing_entries(listing, &raw_entries, &raw_types);

    /* ── Separate into dirs and files, sort each group alphabetically ── */
    /* Worst case: raw_count dirs + raw_count files */
    char **dirs      = malloc((size_t)raw_count * sizeof(*dirs));
    char **files     = malloc((size_t)raw_count * sizeof(*files));
    int    dir_count = 0, file_count = 0;

    for (int i = 0; i < raw_count; i++)
    {
        if (raw_types && raw_types[i] == ETYPE_DIR)
            dirs[dir_count++]   = raw_entries[i];
        else
            files[file_count++] = raw_entries[i];
    }

    /* Simple insertion sort (directories and files are typically small lists). */
    for (int i = 1; i < dir_count; i++) {
        char *key = dirs[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(dirs[j], key) > 0) { dirs[j+1] = dirs[j]; j--; }
        dirs[j+1] = key;
    }
    for (int i = 1; i < file_count; i++) {
        char *key = files[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(files[j], key) > 0) { files[j+1] = files[j]; j--; }
        files[j+1] = key;
    }

    /*
     * ── Build the final display array ───────────────────────────────────
     *
     * Layout:
     *   [0]              MOVE_UP       ".."
     *   [1 .. dir_count] DIR           real directories
     *   [dir_count+1]    NEW_FOLDER    sentinel
     *   [dir_count+2 ..] FILE          real files
     *   [last]           NEW_FILE      sentinel
     */
    int total = 1 + dir_count + 1 + file_count + 1;  /* up + dirs + new_folder + files + new_file */

    char **entries = malloc((size_t)total * sizeof(*entries));
    int   *etypes  = malloc((size_t)total * sizeof(*etypes));

    int idx = 0;
    entries[idx] = "..";               etypes[idx] = ETYPE_MOVE_UP;    idx++;
    for (int i = 0; i < dir_count;  i++) { entries[idx] = dirs[i];  etypes[idx] = ETYPE_DIR;        idx++; }
    entries[idx] = SENTINEL_NEW_FOLDER; etypes[idx] = ETYPE_NEW_FOLDER; idx++;
    for (int i = 0; i < file_count; i++) { entries[idx] = files[i]; etypes[idx] = ETYPE_FILE;       idx++; }
    entries[idx] = SENTINEL_NEW_FILE;   etypes[idx] = ETYPE_NEW_FILE;   idx++;

    free(dirs);
    free(files);

    /* ── Layout maths ────────────────────────────────────────────────── */
    int width            = getmaxx(stdscr);
    int height           = getmaxy(stdscr);
    int available_width  = width - 2;
    int available_height = (height - 2) - LIST_TOP_ROW;
    int cols             = available_width  / TILE_WIDTH;
    int rows             = available_height / TILE_HEIGHT;

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    int per_page   = cols * rows;
    int page_count = (total + per_page - 1) / per_page;

    int page = *current_page_zero_based;
    if (page < 0)          page = 0;
    if (page >= page_count) page = page_count - 1;
    *current_page_zero_based = page;

    /* ── Draw ────────────────────────────────────────────────────────── */
    int start = page * per_page;
    for (int i = 0; i < per_page && (start + i) < total; i++)
    {
        int grid_row = i / cols;
        int grid_col = i % cols;
        int draw_row = LIST_TOP_ROW + (grid_row * TILE_HEIGHT);
        int draw_col = 2 + (grid_col * TILE_WIDTH);

        enum SPRITES sprite;
        const char  *label = entries[start + i];

        switch (etypes[start + i])
        {
            case ETYPE_MOVE_UP:    sprite = MOVE_UP;    label = "..";           break;
            case ETYPE_DIR:        sprite = FOLDER;     break;
            case ETYPE_NEW_FOLDER: sprite = NEW_FOLDER; label = "New Folder";   break;
            case ETYPE_FILE:       sprite = classify_sprite(label, 0);          break;
            case ETYPE_NEW_FILE:   sprite = NEW_FILE;   label = "New File";     break;
            default:               sprite = GENERIC_FILE; break;
        }

        /* Highlight the selected tile with reverse video. */
        int global_idx = start + i;
        if (global_idx == selected_index)
            attron(A_REVERSE);

        draw_sprite(draw_row, draw_col, sprite, entries[start + i]);
        mvprintw(draw_row + SPRITE_HEIGHT, draw_col, "%-20.20s", label);

        if (global_idx == selected_index)
            attroff(A_REVERSE);
    }

    if (total_entries_out)
        *total_entries_out = total;

    free(entries);
    free(etypes);
    free_listing_entries(raw_entries, raw_types, raw_count);
    return page_count;
}

static void show_explorer_footer(void)
{
    int width = getmaxx(stdscr);

    move(getmaxy(stdscr) - 2, 0);
    for (int i = 0; i < width; ++i) addch('-');

    mvprintw(getmaxy(stdscr) - 1, 0,
             "Commands: [Esc] Quit | [R] Refresh | [Arrows] Navigate | [Enter] Open"
             " | [^D] Download | [^U] Upload | [Del] Delete");
}

/* ── Application state ──────────────────────────────────────────────────── */

typedef enum { STATE_LOGIN, STATE_EXPLORER } AppState;

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
    setlocale(LC_ALL, "");

    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, FALSE);
    keypad(stdscr, TRUE);

    /* 0 = username field active, 1 = password field active. */
    int      active_field     = 0;
    AppState app_state        = STATE_LOGIN;
    bool     needs_full_redraw = true;
    char     debug_message[256] = {0};
    debug_message[0] = '\0';
    char *listing = NULL;
    int explorer_page       = 0;
    int explorer_max_pages  = 1;
    int explorer_selected   = 0;   /* Global index of highlighted entry. */
    int explorer_total      = 0;   /* Total number of entries on this listing. */

    /* Current remote directory path (absolute, no trailing slash). */
    char current_path[MAX_PATH_LEN + 1];
    strncpy(current_path, "~", sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    while (1)
    {
        /* ── Render ─────────────────────────────────────────────────────── */
        if (app_state == STATE_LOGIN)
        {
            if (needs_full_redraw)
            {
                clear();
                show_login_labels();

                /*
                 * The inactive field is shown from its stored plain string;
                 * the active field is shown from g_input (live editing slot).
                 * We temporarily swap g_input to render the inactive field,
                 * then restore it — but it's simpler to just print the stored
                 * strings directly for the inactive row.
                 */
                if (active_field == 0)
                {
                    /* username is live in g_input; password comes from stored string */
                    redraw_field(USERNAME_ROW, false);

                    /* Print stored password as stars without touching g_input */
                    move(PASSWORD_ROW, INPUT_COL);
                    clrtoeol();
                    for (int i = 0; password[i]; i++) addch('*');
                }
                else
                {
                    /* password is live in g_input; username comes from stored string */
                    move(USERNAME_ROW, INPUT_COL);
                    clrtoeol();
                    mvprintw(USERNAME_ROW, INPUT_COL, "%s", username);

                    redraw_field(PASSWORD_ROW, true);
                }

                needs_full_redraw = false;
            }

            /* Keep the hardware cursor in the active field. */
            if (active_field == 0)
                move(USERNAME_ROW, INPUT_COL + g_input.cursor_position);
            else
                move(PASSWORD_ROW, INPUT_COL + g_input.cursor_position);

            wrefresh(stdscr);
        }
        else if (app_state == STATE_EXPLORER)
        {
            if (needs_full_redraw)
            {
                clear();
                explorer_max_pages = show_explorer_listing(listing, &explorer_page,
                                                           explorer_selected, &explorer_total);
                show_explorer_header(SERVER_ADDRESS, username, current_path,
                                     explorer_page + 1, explorer_max_pages, debug_message);
                show_explorer_footer();
                wrefresh(stdscr);
                needs_full_redraw = false;
            }
        }

        /* ── Input ──────────────────────────────────────────────────────── */
        int ch = getch();

        if (ch == 27) break; /* Escape – quit. */

        if (ch == KEY_RESIZE)
        {
            needs_full_redraw = true;
            continue;
        }

        if (app_state == STATE_LOGIN)
        {
            if (active_field == 0)
            {
                if (ch == '\n')
                {
                    flush_input_to(username); /* Save username, clear slot. */
                    active_field      = 1;
                    needs_full_redraw = true;
                    /* Load any previously typed password back for editing. */
                    load_input_from(password);
                }
                else if (handle_input(ch))
                {
                    redraw_field(USERNAME_ROW, false);
                }
            }
            else /* active_field == 1 */
            {
                if (ch == '\n')
                {
                    flush_input_to(password); /* Save password, clear slot. */

                    listing = list_directory(
                        SERVER_ADDRESS, SERVER_PORT,
                        username, password, "~");

                    debug_message[0] = '\0';
                    if (listing)
                    {
                        strncat(debug_message, listing, sizeof(debug_message) - 1);
                    }

                    if (listing != NULL)
                    {
                        strncpy(current_path, "~", sizeof(current_path) - 1);
                        current_path[sizeof(current_path) - 1] = '\0';
                        explorer_page      = 0;
                        explorer_max_pages = 1;
                        explorer_selected  = 0;
                        explorer_total     = 0;
                        app_state          = STATE_EXPLORER;
                        needs_full_redraw  = true;
                    }
                    else
                    {
                        mvprintw(PASSWORD_ROW + 1, LABEL_COL,
                                 "Login failed. Press any key to retry.");
                        wrefresh(stdscr);
                        getch();

                        /* Reload password into g_input so the user can edit it. */
                        load_input_from(password);
                        needs_full_redraw = true;
                    }
                }
                else if (handle_input(ch))
                {
                    redraw_field(PASSWORD_ROW, true);
                }
            }
        }
        else if (app_state == STATE_EXPLORER)
        {
            /* ── Compute layout to know cols/rows for arrow navigation ── */
            int width            = getmaxx(stdscr);
            int height           = getmaxy(stdscr);
            int available_width  = width - 2;
            int available_height = (height - 2) - LIST_TOP_ROW;
            int nav_cols         = available_width  / TILE_WIDTH;
            int nav_rows         = available_height / TILE_HEIGHT;
            if (nav_cols < 1) nav_cols = 1;
            if (nav_rows < 1) nav_rows = 1;
            int per_page = nav_cols * nav_rows;

            /* ── Helper: get entry name + type at global index ────────── */
            /* (We re-parse only when we need entry info – lightweight.) */

            if (ch == KEY_UP)
            {
                if (explorer_selected - nav_cols >= 0)
                {
                    explorer_selected -= nav_cols;
                    /* If selected scrolled off current page, go to prev page. */
                    int new_page = explorer_selected / per_page;
                    if (new_page != explorer_page)
                        explorer_page = new_page;
                    needs_full_redraw = true;
                }
            }
            else if (ch == KEY_DOWN)
            {
                if (explorer_selected + nav_cols < explorer_total)
                {
                    explorer_selected += nav_cols;
                    int new_page = explorer_selected / per_page;
                    if (new_page != explorer_page)
                        explorer_page = new_page;
                    needs_full_redraw = true;
                }
            }
            else if (ch == KEY_LEFT)
            {
                if (explorer_selected % nav_cols > 0)
                {
                    explorer_selected--;
                    needs_full_redraw = true;
                }
                else if (explorer_page > 0)
                {
                    /* At left edge: go to previous page, last column. */
                    explorer_page--;
                    int page_start = explorer_page * per_page;
                    int page_end   = page_start + per_page - 1;
                    if (page_end >= explorer_total)
                        page_end = explorer_total - 1;
                    explorer_selected = page_end;
                    needs_full_redraw = true;
                }
            }
            else if (ch == KEY_RIGHT)
            {
                if (explorer_selected % nav_cols < nav_cols - 1 &&
                    explorer_selected + 1 < explorer_total)
                {
                    explorer_selected++;
                    needs_full_redraw = true;
                }
                else if (explorer_page + 1 < explorer_max_pages)
                {
                    /* At right edge: go to next page, first column. */
                    explorer_page++;
                    explorer_selected = explorer_page * per_page;
                    needs_full_redraw = true;
                }
            }
            else if (ch == KEY_HOME)
            {
                explorer_selected = 0;
                explorer_page     = 0;
                needs_full_redraw = true;
            }
            else if (ch == KEY_END)
            {
                explorer_selected = explorer_total > 0 ? explorer_total - 1 : 0;
                explorer_page     = explorer_selected / per_page;
                needs_full_redraw = true;
            }
            else if (ch == KEY_PPAGE) /* Page Up */
            {
                if (explorer_page > 0)
                {
                    explorer_page--;
                    explorer_selected = explorer_page * per_page;
                    needs_full_redraw = true;
                }
            }
            else if (ch == KEY_NPAGE) /* Page Down */
            {
                if (explorer_page + 1 < explorer_max_pages)
                {
                    explorer_page++;
                    explorer_selected = explorer_page * per_page;
                    needs_full_redraw = true;
                }
            }
            /* ── Enter: open selected entry ──────────────────────────── */
            else if (ch == '\n' || ch == KEY_ENTER)
            {
                /* Re-parse listing to find the selected entry. */
                char **raw_entries = NULL;
                int   *raw_types   = NULL;
                int    raw_count   = parse_listing_entries(listing,
                                                           &raw_entries, &raw_types);

                /* Rebuild the sorted display order (mirrors show_explorer_listing). */
                char **dirs  = malloc((size_t)raw_count * sizeof(*dirs));
                char **files = malloc((size_t)raw_count * sizeof(*files));
                int    dc = 0, fc = 0;
                for (int i = 0; i < raw_count; i++) {
                    if (raw_types && raw_types[i] == ETYPE_DIR) dirs[dc++]  = raw_entries[i];
                    else                                         files[fc++] = raw_entries[i];
                }
                for (int i = 1; i < dc; i++) {
                    char *k = dirs[i]; int j = i - 1;
                    while (j >= 0 && strcasecmp(dirs[j], k) > 0) { dirs[j+1] = dirs[j]; j--; }
                    dirs[j+1] = k;
                }
                for (int i = 1; i < fc; i++) {
                    char *k = files[i]; int j = i - 1;
                    while (j >= 0 && strcasecmp(files[j], k) > 0) { files[j+1] = files[j]; j--; }
                    files[j+1] = k;
                }

                /* total layout: [MOVE_UP] [dirs...] [NEW_FOLDER] [files...] [NEW_FILE] */
                int sel = explorer_selected;
                const char *sel_name = NULL;
                int         sel_type = -1;  /* ETYPE_* */

                if (sel == 0) {
                    sel_name = "..";
                    sel_type = ETYPE_MOVE_UP;
                } else if (sel <= dc) {
                    sel_name = dirs[sel - 1];
                    sel_type = ETYPE_DIR;
                } else if (sel == dc + 1) {
                    sel_type = ETYPE_NEW_FOLDER;
                } else if (sel <= dc + 1 + fc) {
                    sel_name = files[sel - dc - 2];
                    sel_type = ETYPE_FILE;
                } else {
                    sel_type = ETYPE_NEW_FILE;
                }

                if (sel_type == ETYPE_MOVE_UP)
                {
                    /* Navigate to parent directory. */
                    char parent[MAX_PATH_LEN + 1];
                    strncpy(parent, current_path, sizeof(parent) - 1);
                    parent[sizeof(parent) - 1] = '\0';
                    char *slash = strrchr(parent, '/');
                    if (slash && slash != parent)
                        *slash = '\0';
                    else if (slash == parent)
                        slash[1] = '\0';  /* root "/" */
                    else
                        strncpy(parent, "/", sizeof(parent) - 1);

                    char *new_listing = list_directory(
                        SERVER_ADDRESS, SERVER_PORT, username, password, parent);
                    if (new_listing) {
                        free(listing);
                        listing = new_listing;
                        strncpy(current_path, parent, sizeof(current_path) - 1);
                        current_path[sizeof(current_path) - 1] = '\0';
                        explorer_page     = 0;
                        explorer_selected = 0;
                        needs_full_redraw = true;
                    }
                }
                else if (sel_type == ETYPE_DIR && sel_name)
                {
                    /* Navigate into subdirectory. */
                    char new_path[MAX_PATH_LEN + 1];
                    path_join(new_path, sizeof(new_path), current_path, sel_name);

                    char *new_listing = list_directory(
                        SERVER_ADDRESS, SERVER_PORT, username, password, new_path);
                    if (new_listing) {
                        free(listing);
                        listing = new_listing;
                        strncpy(current_path, new_path, sizeof(current_path) - 1);
                        current_path[sizeof(current_path) - 1] = '\0';
                        explorer_page     = 0;
                        explorer_selected = 0;
                        needs_full_redraw = true;
                    }
                }
                else if (sel_type == ETYPE_FILE && sel_name)
                {
                    /* Download the selected file. */
                    char remote_path[MAX_PATH_LEN + 1];
                    path_join(remote_path, sizeof(remote_path), current_path, sel_name);

                    char download_dir[MAX_PATH_LEN + 1];
                    if (!get_default_download_dir(download_dir, sizeof(download_dir)))
                        strncpy(download_dir, ".", sizeof(download_dir) - 1);

                    char saved_path[MAX_PATH_LEN + 1] = {0};
                    int rc = download_from_server(SERVER_ADDRESS, SERVER_PORT,
                                                  username, password,
                                                  remote_path, download_dir, saved_path);

                    /* Brief status overlay at the bottom. */
                    move(getmaxy(stdscr) - 1, 0);
                    clrtoeol();
                    if (rc == ERR_NONE)
                        mvprintw(getmaxy(stdscr) - 1, 0,
                                 "Downloaded → %s  (press any key)", saved_path);
                    else
                        mvprintw(getmaxy(stdscr) - 1, 0,
                                 "Download failed (err=%d)  (press any key)", rc);
                    wrefresh(stdscr);
                    getch();
                    needs_full_redraw = true;
                }

                free(dirs);
                free(files);
                free_listing_entries(raw_entries, raw_types, raw_count);
            }
            /* ── Ctrl+D: download selected entry ─────────────────────── */
            else if (ch == 4) /* Ctrl+D */
            {
                char **raw_entries = NULL;
                int   *raw_types   = NULL;
                int    raw_count   = parse_listing_entries(listing,
                                                           &raw_entries, &raw_types);
                char **dirs  = malloc((size_t)raw_count * sizeof(*dirs));
                char **files = malloc((size_t)raw_count * sizeof(*files));
                int    dc = 0, fc = 0;
                for (int i = 0; i < raw_count; i++) {
                    if (raw_types && raw_types[i] == ETYPE_DIR) dirs[dc++]  = raw_entries[i];
                    else                                         files[fc++] = raw_entries[i];
                }
                for (int i = 1; i < dc; i++) {
                    char *k = dirs[i]; int j = i - 1;
                    while (j >= 0 && strcasecmp(dirs[j], k) > 0) { dirs[j+1] = dirs[j]; j--; }
                    dirs[j+1] = k;
                }
                for (int i = 1; i < fc; i++) {
                    char *k = files[i]; int j = i - 1;
                    while (j >= 0 && strcasecmp(files[j], k) > 0) { files[j+1] = files[j]; j--; }
                    files[j+1] = k;
                }

                int sel = explorer_selected;
                const char *sel_name = NULL;
                int         sel_type = -1;

                if (sel == 0) {
                    sel_type = ETYPE_MOVE_UP;
                } else if (sel <= dc) {
                    sel_name = dirs[sel - 1];
                    sel_type = ETYPE_DIR;
                } else if (sel == dc + 1) {
                    sel_type = ETYPE_NEW_FOLDER;
                } else if (sel <= dc + 1 + fc) {
                    sel_name = files[sel - dc - 2];
                    sel_type = ETYPE_FILE;
                } else {
                    sel_type = ETYPE_NEW_FILE;
                }

                move(getmaxy(stdscr) - 1, 0);
                clrtoeol();

                if ((sel_type == ETYPE_FILE || sel_type == ETYPE_DIR) && sel_name)
                {
                    char remote_path[MAX_PATH_LEN + 1];
                    path_join(remote_path, sizeof(remote_path), current_path, sel_name);

                    char download_dir[MAX_PATH_LEN + 1];
                    if (!get_default_download_dir(download_dir, sizeof(download_dir)))
                        strncpy(download_dir, ".", sizeof(download_dir) - 1);

                    char saved_path[MAX_PATH_LEN + 1] = {0};
                    int rc = download_from_server(SERVER_ADDRESS, SERVER_PORT,
                                                  username, password,
                                                  remote_path, download_dir, saved_path);

                    if (rc == ERR_NONE)
                        mvprintw(getmaxy(stdscr) - 1, 0,
                                 "Downloaded → %s  (press any key)", saved_path);
                    else
                        mvprintw(getmaxy(stdscr) - 1, 0,
                                 "Download failed (err=%d)  (press any key)", rc);
                }
                else
                {
                    mvprintw(getmaxy(stdscr) - 1, 0,
                             "Select a file or directory to download.  (press any key)");
                }
                wrefresh(stdscr);
                getch();
                needs_full_redraw = true;

                free(dirs);
                free(files);
                free_listing_entries(raw_entries, raw_types, raw_count);
            }
            /* ── Ctrl+U: upload a local file to current directory ─────── */
            else if (ch == 21) /* Ctrl+U */
            {
                /* Prompt for a local file path using the shared input slot. */
                memset(&g_input, 0, sizeof g_input);
                g_input.max_length = INPUT_BUF_MAX;

                int prompt_row = getmaxy(stdscr) - 1;
                move(prompt_row, 0);
                clrtoeol();
                mvprintw(prompt_row, 0, "Upload local file: ");
                wrefresh(stdscr);
                echo();
                curs_set(1);

                int done = 0;
                while (!done)
                {
                    int uch = getch();
                    if (uch == '\n')
                    {
                        done = 1;
                    }
                    else if (uch == 27) /* Escape – cancel */
                    {
                        memset(&g_input, 0, sizeof g_input);
                        g_input.max_length = INPUT_BUF_MAX;
                        done = -1;
                    }
                    else if (handle_input(uch))
                    {
                        /* Redraw the prompt + typed text. */
                        move(prompt_row, 0);
                        clrtoeol();
                        mvprintw(prompt_row, 0, "Upload local file: %s", g_input.buffer);
                        move(prompt_row, 19 + g_input.cursor_position);
                        wrefresh(stdscr);
                    }
                }
                noecho();
                curs_set(0);

                if (done == 1 && g_input.length > 0)
                {
                    char local_file[INPUT_BUF_MAX];
                    flush_input_to(local_file);

                    /* Build the remote target path: current_path/basename(local_file) */
                    const char *basename_start = strrchr(local_file, '/');
                    const char *base = basename_start ? basename_start + 1 : local_file;
                    char remote_target[MAX_PATH_LEN + 1];
                    path_join(remote_target, sizeof(remote_target), current_path, base);

                    int rc = upload_to_server(SERVER_ADDRESS, SERVER_PORT,
                                             username, password,
                                             local_file, remote_target);

                    move(prompt_row, 0);
                    clrtoeol();
                    if (rc == ERR_NONE)
                        mvprintw(prompt_row, 0,
                                 "Uploaded → %s  (press any key)", remote_target);
                    else
                        mvprintw(prompt_row, 0,
                                 "Upload failed (err=%d)  (press any key)", rc);
                    wrefresh(stdscr);
                    getch();

                    /* Refresh listing to show the new file. */
                    char *new_listing = list_directory(SERVER_ADDRESS, SERVER_PORT,
                                                       username, password, current_path);
                    if (new_listing) {
                        free(listing);
                        listing = new_listing;
                        explorer_page = 0;
                    }
                }
                else
                {
                    /* Cancelled or empty – restore the footer. */
                    memset(&g_input, 0, sizeof g_input);
                    g_input.max_length = INPUT_BUF_MAX;
                }

                needs_full_redraw = true;
            }
            /* ── Delete key: delete selected entry with confirmation ──── */
            else if (ch == KEY_DC || ch == KEY_BACKSPACE || ch == 127)
            {
                /* Only treat KEY_DC (true Delete) as the delete trigger;
                 * Backspace is intentionally not wired here so it doesn't
                 * accidentally delete entries.  Use only KEY_DC. */
                if (ch != KEY_DC)
                    goto skip_delete;

                char **raw_entries = NULL;
                int   *raw_types   = NULL;
                int    raw_count   = parse_listing_entries(listing,
                                                           &raw_entries, &raw_types);
                char **dirs  = malloc((size_t)raw_count * sizeof(*dirs));
                char **files = malloc((size_t)raw_count * sizeof(*files));
                int    dc = 0, fc = 0;
                for (int i = 0; i < raw_count; i++) {
                    if (raw_types && raw_types[i] == ETYPE_DIR) dirs[dc++]  = raw_entries[i];
                    else                                         files[fc++] = raw_entries[i];
                }
                for (int i = 1; i < dc; i++) {
                    char *k = dirs[i]; int j = i - 1;
                    while (j >= 0 && strcasecmp(dirs[j], k) > 0) { dirs[j+1] = dirs[j]; j--; }
                    dirs[j+1] = k;
                }
                for (int i = 1; i < fc; i++) {
                    char *k = files[i]; int j = i - 1;
                    while (j >= 0 && strcasecmp(files[j], k) > 0) { files[j+1] = files[j]; j--; }
                    files[j+1] = k;
                }

                int sel = explorer_selected;
                const char *sel_name = NULL;
                int         sel_type = -1;

                if (sel == 0) {
                    sel_type = ETYPE_MOVE_UP;
                } else if (sel <= dc) {
                    sel_name = dirs[sel - 1];
                    sel_type = ETYPE_DIR;
                } else if (sel == dc + 1) {
                    sel_type = ETYPE_NEW_FOLDER;
                } else if (sel <= dc + 1 + fc) {
                    sel_name = files[sel - dc - 2];
                    sel_type = ETYPE_FILE;
                } else {
                    sel_type = ETYPE_NEW_FILE;
                }

                int prompt_row = getmaxy(stdscr) - 1;
                move(prompt_row, 0);
                clrtoeol();

                if ((sel_type == ETYPE_FILE || sel_type == ETYPE_DIR) && sel_name)
                {
                    char remote_path[MAX_PATH_LEN + 1];
                    path_join(remote_path, sizeof(remote_path), current_path, sel_name);

                    mvprintw(prompt_row, 0,
                             "Delete '%s'? [y/N]: ", remote_path);
                    wrefresh(stdscr);
                    int confirm = getch();

                    move(prompt_row, 0);
                    clrtoeol();

                    if (confirm == 'y' || confirm == 'Y')
                    {
                        int rc = delete_from_server(SERVER_ADDRESS, SERVER_PORT,
                                                    username, password, remote_path);

                        if (rc == ERR_NONE)
                            mvprintw(prompt_row, 0,
                                     "Deleted '%s'.  (press any key)", sel_name);
                        else
                            mvprintw(prompt_row, 0,
                                     "Delete failed (err=%d).  (press any key)", rc);
                        wrefresh(stdscr);
                        getch();

                        /* Refresh listing. */
                        char *new_listing = list_directory(SERVER_ADDRESS, SERVER_PORT,
                                                           username, password, current_path);
                        if (new_listing) {
                            free(listing);
                            listing = new_listing;
                            if (explorer_selected > 0 && rc == ERR_NONE)
                                explorer_selected--;
                            explorer_page = explorer_selected / per_page;
                        }
                    }
                    else
                    {
                        mvprintw(prompt_row, 0, "Delete cancelled.");
                        wrefresh(stdscr);
                        napms(600);
                    }
                }
                else
                {
                    mvprintw(prompt_row, 0,
                             "Select a file or directory to delete.  (press any key)");
                    wrefresh(stdscr);
                    getch();
                }

                free(dirs);
                free(files);
                free_listing_entries(raw_entries, raw_types, raw_count);
                needs_full_redraw = true;

                skip_delete: ;
            }
            else if (ch == 'r' || ch == 'R')
            {
                char *new_listing = list_directory(
                    SERVER_ADDRESS, SERVER_PORT,
                    username, password, current_path);

                if (new_listing)
                {
                    free(listing);
                    listing = new_listing;
                    explorer_page     = 0;
                    explorer_selected = 0;
                    needs_full_redraw = true;
                }
            }
        }
    }

    free(listing);
    endwin();
    return 0;
}
