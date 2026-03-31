#ifdef _WIN32
	#include <curses.h>
#else
	#include <ncurses.h>
#endif

#include "./send-file-socket.c"

/* ── Config ────────────────────────────────────────────────────────────── */

#define MAX_WINDOWS	32

#define HOST			"192.168.1.102" // "10.0.0.1"
#define PORT			"9001"

/* ── Global Variables ─────────────────────────────────────────────────── */

char username[256] = "";
char password[256] = "";
char current_dir[1024] = "~";

WINDOW *windows[MAX_WINDOWS] = {NULL};

/* ── Helpers ───────────────────────────────────────────────────────────── */

/**
 * Creates a new window with the specified dimensions and position,
 * and stores it in the global windows array.
 * 
 * @param height The height of the window.
 * @param width The width of the window.
 * @param starty The starting y-coordinate of the window.
 * @param startx The starting x-coordinate of the window.
 * @return A pointer to the created window, or NULL if the maximum number of windows is reached.
 */
WINDOW* createWindow(int height, int width, int starty, int startx) {
    
    WINDOW *win = newwin(height, width, starty, startx);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] == NULL) {
            windows[i] = win;
            return win;
        }
    }
    fprintf(stderr, "Error: Maximum number of windows reached.\n");
    return NULL;
}

void disposeWindow(WINDOW *win) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] == win) {
            delwin(win);
            windows[i] = NULL;
            return;
        }
    }
    fprintf(stderr, "Error: Window not found in global array.\n");
}

void displayLogin(WINDOW *hostwin) {
    int maxy, maxx;
    getmaxyx(hostwin, maxy, maxx);

    
    WINDOW *loginwin = createWindow(6, maxx - 2, maxy - 7, 1);
    if (!loginwin) {
        endwin();

        fprintf(stderr, "Error creating window.\n");
        exit(1);
    }
    box(loginwin, 0, 0);

    clear();
    mvwprintw(loginwin, 1, 2, "=== LOGIN ===");
    mvwprintw(loginwin, 3, 2, "Username: %s", username);
    mvwprintw(loginwin, 4, 2, "Password: %s", password);
    wrefresh(loginwin);

    echo();
    mvwgetnstr(loginwin, 3, 12 + strlen(username), username, sizeof(username) - 1);
    noecho();
    mvwgetnstr(loginwin, 4, 12 + strlen(password), password, sizeof(password) - 1);
    wclear(loginwin);
    box(loginwin, 0, 0);
    mvwprintw(loginwin, 1, 2, "Logging in with username '%s'...", username);
    wrefresh(loginwin);
    wgetch(loginwin);
    disposeWindow(loginwin);
}

/* ── Main ───────────────────────────────────────────────────────────── */

// TODO:    Make input non blocking using nodelay() and handle input in a loop
//          Generalize window management

int main(void)
{
    initscr();
    windows[0] = stdscr;
    box(stdscr, 0, 0);
    refresh();

    noecho();    /* Don't echo typed characters */
    cbreak();    /* Don't buffer keystrokes */
    keypad(stdscr, TRUE);

    displayLogin(stdscr);

    int ch = getch();
    disposeWindow(windows[0]);
    endwin();
    return 0;
}
