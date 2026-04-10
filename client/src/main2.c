#include "./send-file-socket.c"

#include <ncurses.h>
#include <string.h>

/* ── DEBUG ────────────────────────────────────────────────────── */
#define DEBUG 1

/* ── Network target ────────────────────────────────────────────────────── */
#define SERVER_ADDRESS "192.168.1.102"
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

static void show_explorer_footer(void)
{
    int width = getmaxx(stdscr);

    move(getmaxy(stdscr) - 2, 0);
    for (int i = 0; i < width; ++i) addch('-');

    // TODO: Implement Refresh and Move, and Pagination, and everything except Esc to Quit.
    mvprintw(getmaxy(stdscr) - 1, 0, "Commands: [Esc] Quit | [R] Refresh | [Up/Down] Navigate");
}

/* ── Application state ──────────────────────────────────────────────────── */

typedef enum { STATE_LOGIN, STATE_EXPLORER } AppState;

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
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
                show_explorer_header(SERVER_ADDRESS, username, "~", 1, 1, debug_message);
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
                        clear();
                        mvprintw(0, 0, "Directory listing:\n%s", listing);
                        free(listing);
                        wrefresh(stdscr);
                        app_state         = STATE_EXPLORER;
                        needs_full_redraw = true;
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
        /* STATE_EXPLORER input handling goes here in a future iteration. */
    }

    endwin();
    return 0;
}