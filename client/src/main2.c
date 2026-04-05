#include "./send-file-socket.c"

#include <ncurses.h>
#include <string.h>

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
 * Encapsulates everything needed to manage a single text-input field:
 *
 *   buffer          – Null-terminated character array holding the typed text.
 *   length          – Number of characters currently in the buffer
 *                     (excludes the null terminator).
 *   max_length      – Hard capacity of `buffer`, including the null
 *                     terminator.  Characters are rejected once
 *                     length >= max_length - 1.
 *   cursor_position – Logical index within `buffer` at which the next
 *                     keystroke will be inserted (0 … length, inclusive).
 *                     Moving left/right adjusts this independently of the
 *                     ncurses screen cursor, which is re-synced after every
 *                     redraw.
 * ═══════════════════════════════════════════════════════════════════════════ */
struct input_state
{
    char buffer[INPUT_BUF_MAX];
    int  length;
    int  max_length;
    int  cursor_position;
};

/* ── Helpers ────────────────────────────────────────────────────────────── */

/**
 * redraw_field – Repaints a single input field in place.
 *
 * Moves to (row, INPUT_COL), clears to end-of-line, then reprints every
 * character (or '*' when hide_input is true).  Finally positions the
 * screen cursor at the logical cursor_position so ncurses blinking cursor
 * matches where the next character will land.
 *
 * Separating drawing from input handling avoids the fragile "print one
 * char at a time and hope the cursor stays in sync" approach.
 */
static void redraw_field(int row, bool hide_input, const struct input_state *state)
{
    move(row, INPUT_COL);
    clrtoeol();

    for (int i = 0; i < state->length; i++)
        addch(hide_input ? '*' : (unsigned char)state->buffer[i]);

    /* Re-position the hardware cursor to match the logical cursor. */
    move(row, INPUT_COL + state->cursor_position);
}

/**
 * handle_input – Applies one keypress to an input_state.
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
static bool handle_input(int ch, struct input_state *state)
{
    if (ch == KEY_BACKSPACE || ch == 127)
    {
        /* Nothing to delete when cursor is already at the start. */
        if (state->cursor_position == 0)
            return false;

        /*
         * Shift every character from cursor onward one step to the left,
         * overwriting the character that was immediately left of the cursor.
         */
        int del = state->cursor_position - 1;
        memmove(&state->buffer[del],
                &state->buffer[del + 1],
                (size_t)(state->length - del)); /* includes '\0' */

        state->cursor_position--;
        state->length--;
        return true;
    }

    if (ch == KEY_LEFT)
    {
        if (state->cursor_position > 0)
        {
            state->cursor_position--;
            return true;
        }
        return false;
    }

    if (ch == KEY_RIGHT)
    {
        /* Cursor may reach `length` (after the last char) but no further. */
        if (state->cursor_position < state->length)
        {
            state->cursor_position++;
            return true;
        }
        return false;
    }

    /*
     * Accept any non-control byte: standard printable ASCII (32–126),
     * DEL is excluded (127 = backspace alias, handled above), and all
     * high bytes (128–255) which are valid UTF-8 continuation or lead
     * bytes delivered one at a time by ncurses.
     *
     * This matters for passwords containing multibyte characters such as
     * «, », é, ü, etc.  ncurses splits each UTF-8 sequence into
     * individual getch() calls, so we store the raw bytes and let the
     * server interpret the encoding.
     */
    if ((ch >= 32 && ch < 127) || (ch >= 128 && ch <= 255))
    {
        if (state->length >= state->max_length - 1)
            return false; /* Buffer full – silently drop. */

        /*
         * Make room: shift everything from cursor to end one step right,
         * then write the new character.  memmove handles the +1 for '\0'.
         */
        memmove(&state->buffer[state->cursor_position + 1],
                &state->buffer[state->cursor_position],
                (size_t)(state->length - state->cursor_position + 1));

        state->buffer[state->cursor_position] = (char)ch;
        state->cursor_position++;
        state->length++;
        return true;
    }

    return false; /* Unrecognised key – no change. */
}

/* ── Login screen ───────────────────────────────────────────────────────── */

/**
 * show_login_labels – Draws the static "Username:" / "Password:" labels.
 *
 * Called once on entry and whenever the screen needs a full repaint.
 * Field contents are rendered separately via redraw_field().
 */
static void show_login_labels(void)
{
    mvprintw(USERNAME_ROW, LABEL_COL, "Username: ");
    mvprintw(PASSWORD_ROW, LABEL_COL, "Password: ");
}

/* ── Application state ──────────────────────────────────────────────────── */

typedef enum
{
    STATE_LOGIN,
    STATE_EXPLORER
} AppState;

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
    initscr();
    noecho();            /* Don't echo keystrokes – we render them ourselves. */
    cbreak();            /* Deliver keys immediately, without line buffering.  */
    nodelay(stdscr, FALSE); /* Block in getch() until a key arrives.          */
    keypad(stdscr, TRUE);   /* Enable arrow-key translation.                  */

    static struct input_state username_state = {
        .buffer          = {0},
        .length          = 0,
        .max_length      = INPUT_BUF_MAX,
        .cursor_position = 0
    };
    static struct input_state password_state = {
        .buffer          = {0},
        .length          = 0,
        .max_length      = INPUT_BUF_MAX,
        .cursor_position = 0
    };

    /* 0 = username field active, 1 = password field active. */
    int active_field = 0;

    AppState app_state = STATE_LOGIN;

    /* Force a full redraw on the very first iteration. */
    bool needs_full_redraw = true;

    while (1)
    {
        /* ── Render ─────────────────────────────────────────────────────── */
        if (app_state == STATE_LOGIN)
        {
            if (needs_full_redraw)
            {
                clear();
                show_login_labels();

                /* Repaint both fields so nothing is lost after a clear(). */
                redraw_field(USERNAME_ROW, false, &username_state);
                redraw_field(PASSWORD_ROW, true,  &password_state);

                needs_full_redraw = false;
            }

            /* Always position the cursor in the active field. */
            if (active_field == 0)
                move(USERNAME_ROW, INPUT_COL + username_state.cursor_position);
            else
                move(PASSWORD_ROW, INPUT_COL + password_state.cursor_position);

            wrefresh(stdscr);
        }

        /* ── Input ──────────────────────────────────────────────────────── */
        int ch = getch();

        if (ch == 27) /* Escape – quit. */
            break;

        if (app_state == STATE_LOGIN)
        {
            if (active_field == 0)
            {
                if (ch == '\n')
                {
                    /* Advance to the password field. */
                    active_field      = 1;
                    needs_full_redraw = true;
                }
                else if (handle_input(ch, &username_state))
                {
                    redraw_field(USERNAME_ROW, false, &username_state);
                }
            }
            else /* active_field == 1 */
            {
                if (ch == '\n')
                {
                    /*
                     * Attempt login: ask the server for the root directory
                     * listing using the supplied credentials.
                     */
                    char *listing = list_directory(
                        SERVER_ADDRESS, SERVER_PORT,
                        username_state.buffer,
                        password_state.buffer,
                        "~"); /* List the user's home directory. */

                    if (listing != NULL)
                    {
                        /* TODO: transition to STATE_EXPLORER and display
                         * `listing` there.  For now, print it inline. */
                        clear();
                        mvprintw(0, 0, "Directory listing:\n%s", listing);
                        free(listing);
                        wrefresh(stdscr);

                        app_state = STATE_EXPLORER;
                    }
                    else
                    {
                        /*
                         * Login failed – show a brief error and let the
                         * user try again without losing what they typed.
                         */
                        mvprintw(PASSWORD_ROW + 1, LABEL_COL,
                                 "Login failed. Press any key to retry.");
                        wrefresh(stdscr);
                        getch(); /* Wait for acknowledgement. */

                        needs_full_redraw = true;
                    }
                }
                else if (handle_input(ch, &password_state))
                {
                    redraw_field(PASSWORD_ROW, true, &password_state);
                }
            }
        }
        /* STATE_EXPLORER input handling goes here in a future iteration. */
    }

    endwin();
    return 0;
}