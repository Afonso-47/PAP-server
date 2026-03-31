#ifdef _WIN32
    #include <curses.h>
    #include <windows.h>
#else
    #include <ncurses.h>
    #include <unistd.h>
#endif

#include <time.h>

#include "./send-file-socket.c"

static double now_seconds(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}


int main(void)
{
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);
    refresh();


    bool running = true;
    double delta = 0.0;

    double last_frame_time = now_seconds();
    double last_update_time = last_frame_time;
    int frame_count = 0;
    int fps = 0;
    double elapsed_seconds = 0.0;


    while (running) {
        double frame_start = now_seconds();

        int ch = getch();
        if (ch != ERR) {
            // Handle input here
            if (ch == 'q' || ch == 'Q') {
                running = false;
            }
        }

        // Update windows and UI here
        wclear(stdscr);
        frame_count++;

        // update values every second
        if (frame_start - last_update_time >= 1.0) {
            fps = frame_count;
            elapsed_seconds = frame_start;
            frame_count = 0;
            last_update_time = frame_start;
        }
        mvprintw(0, 0, "Press 'Q' to quit.\nfps: %d", fps);
        refresh();

        double frame_end = now_seconds();
        delta = frame_end - last_frame_time;
        last_frame_time = frame_end;

        if (delta < 1.0 / 60.0) {
            #ifdef _WIN32
                Sleep((unsigned int)((1.0 / 60.0 - delta) * 1000));
            #else
                usleep((unsigned int)((1.0 / 60.0 - delta) * 1000000));
            #endif
        }
    }

    endwin();
    return 0;
}