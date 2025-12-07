import curses

def main(stdscr):
    # Clear screen
    stdscr.clear()

    # Initialize colors if the terminal supports them. Note: pair 0 is the
    # default pair and cannot be initialized with init_pair on many curses
    # implementations (it returns ERR). Use pairs starting at 1.
    use_colors = False
    if curses.has_colors():
        try:
            curses.start_color()
            # Try to initialize a few color pairs starting at 1.
            curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLACK)
            curses.init_pair(2, curses.COLOR_RED, curses.COLOR_BLACK)
            curses.init_pair(3, curses.COLOR_GREEN, curses.COLOR_BLACK)
            curses.init_pair(4, curses.COLOR_BLUE, curses.COLOR_BLACK)
            use_colors = True
        except curses.error:
            # Some terminals (or Windows builds) may not support these ops.
            use_colors = False

    # Display text with different colors (fall back to no attributes if needed)
    if use_colors:
        stdscr.addstr(0, 0, "This is white text", curses.color_pair(1))
        stdscr.addstr(1, 0, "This is red text", curses.color_pair(2))
        stdscr.addstr(2, 0, "This is green text", curses.color_pair(3))
        stdscr.addstr(3, 0, "This is blue text", curses.color_pair(4))
    else:
        stdscr.addstr(0, 0, "This is white text")
        stdscr.addstr(1, 0, "This is red text")
        stdscr.addstr(2, 0, "This is green text")
        stdscr.addstr(3, 0, "This is blue text")

    # Refresh the screen to show changes
    stdscr.refresh()

    # Wait for user input before exiting
    stdscr.getch()

if __name__ == "__main__":
    curses.wrapper(main)