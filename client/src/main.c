#ifdef _WIN32
	#include <curses.h>
#else
	#include <ncurses.h>
#endif

#include "./send-file-socket.c"

/* ── Config ────────────────────────────────────────────────────────────── */

#define HOST			"192.168.1.102"
#define PORT			"9001"

char username[256] = "";
char password[256] = "";
char current_dir[1024] = "~";

/* ── Helpers ───────────────────────────────────────────────────────────── */

void displayLogin(WINDOW *hostwin) {
    int maxy, maxx;
    getmaxyx(hostwin, maxy, maxx);

    WINDOW *loginwin = newwin(6, maxx - 2, maxy - 6, 2);
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
    delwin(loginwin);
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(void)
{
    initscr();
    box(stdscr, 0, 0);
    refresh();

    noecho();    /* Don't echo typed characters */
    cbreak();    /* Don't buffer keystrokes */
    keypad(stdscr, TRUE);

    WINDOW *hostwin = newwin(LINES - 6, COLS - 2, 1, 1);
    if (!hostwin) {
        endwin();
        fprintf(stderr, "Error creating window.\n");
        exit(1);
    }
    box(hostwin, 0, 0);
    wrefresh(hostwin);

    displayLogin(hostwin);

    int ch = getch();
    endwin();
    return 0;
}
