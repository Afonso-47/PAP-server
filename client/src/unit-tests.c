#ifdef _WIN32
	#include <curses.h>
#else
	#include <ncurses.h>
#endif

#include "./send-file-socket.c"

/* ── Config ────────────────────────────────────────────────────────────── */

#define HOST			"10.0.0.1"
#define PORT			"9001"
#define USERNAME		"root"
#define PASSWORD		"ServerXONXOmj7576«p»"
#define LOCAL_FILE		"./test.txt"
#define REMOTE_TARGET	"/root/temp.txt"
#define DOWNLOAD_DIR	"./downloads"
#define LIST_PATH		"/root"

/* ── Helpers ───────────────────────────────────────────────────────────── */

/**
 * print_err_bits - Print a human-readable breakdown of a bitmask error code.
 *
 * @param rc  Return value from one of the high-level API functions.
 */
static void print_err_bits(int rc)
{
	if (rc == ERR_NONE) {
		printw("  [OK] No errors.\n");
		return;
	}
	if (rc & ERR_PATH_EXPAND)  printw("  [ERR] ERR_PATH_EXPAND  – tilde expansion failed\n");
	if (rc & ERR_CONNECT)      printw("  [ERR] ERR_CONNECT      – could not connect to server\n");
	if (rc & ERR_UNLOCK)       printw("  [ERR] ERR_UNLOCK       – send_unlock() failed\n");
	if (rc & ERR_PATH)         printw("  [ERR] ERR_PATH         – sending username failed\n");
	if (rc & ERR_MODE)         printw("  [ERR] ERR_MODE         – sending mode byte failed\n");
	if (rc & ERR_REMOTE_PATH)  printw("  [ERR] ERR_REMOTE_PATH  – sending remote path failed\n");
	if (rc & ERR_TRANSFER)     printw("  [ERR] ERR_TRANSFER     – file transfer failed\n");
	if (rc & ERR_AUTH)         printw("  [ERR] ERR_AUTH         – authentication failed\n");
}

/**
 * wait_key - Print a prompt and block until the user presses any key.
 */
static void wait_key(void)
{
	printw("\nPress any key to continue...");
	refresh();
	getch();
}

/* ── Test runners ──────────────────────────────────────────────────────── */

/**
 * test_upload - Test upload_to_server().
 *
 * Sends LOCAL_FILE to the server at REMOTE_TARGET.
 * Prints the bitmask error breakdown regardless of outcome.
 */
static void test_upload(void)
{
	clear();
	printw("=== TEST 1: upload_to_server() ===\n\n");
	printw("  local_file    : %s\n", LOCAL_FILE);
	printw("  remote_target : %s\n", REMOTE_TARGET);
	printw("  host          : %s:%s\n", HOST, PORT);
	printw("  username      : %s\n\n", USERNAME);

	/* Sanity-check: make sure the local file actually exists first. */
	FILE *fp = fopen(LOCAL_FILE, "rb");
	if (!fp) {
		printw("  [SKIP] Cannot open '%s' – file does not exist.\n", LOCAL_FILE);
		wait_key();
		return;
	}
	fclose(fp);

	printw("Uploading...\n");
	refresh();

	int rc = upload_to_server(HOST, PORT, USERNAME, PASSWORD, LOCAL_FILE, REMOTE_TARGET);

	printw("\nResult (rc = %d):\n", rc);
	print_err_bits(rc);

	wait_key();
}

/**
 * files_identical - Return 1 if two files have identical contents, 0 if not,
 *                   -1 if either file could not be opened.
 *
 * @param path_a  Path to first file.
 * @param path_b  Path to second file.
 */
static int files_identical(const char *path_a, const char *path_b)
{
	FILE *fa = fopen(path_a, "rb");
	FILE *fb = fopen(path_b, "rb");

	if (!fa || !fb) {
		if (fa) fclose(fa);
		if (fb) fclose(fb);
		return -1;
	}

	unsigned char buf_a[BUFFER_SIZE], buf_b[BUFFER_SIZE];
	int result = 1;

	while (1) {
		size_t na = fread(buf_a, 1, sizeof(buf_a), fa);
		size_t nb = fread(buf_b, 1, sizeof(buf_b), fb);

		if (na != nb || memcmp(buf_a, buf_b, na) != 0) {
			result = 0;
			break;
		}
		if (na == 0)
			break;
	}

	fclose(fa);
	fclose(fb);
	return result;
}

/**
 * test_download - Test download_from_server().
 *
 * Fetches REMOTE_TARGET from the server, saves it into DOWNLOAD_DIR,
 * then compares it byte-for-byte against the original LOCAL_FILE.
 */
static void test_download(void)
{
	clear();
	printw("=== TEST 2: download_from_server() ===\n\n");
	printw("  remote_path   : %s\n", REMOTE_TARGET);
	printw("  output_dir    : %s\n", DOWNLOAD_DIR);
	printw("  host          : %s:%s\n", HOST, PORT);
	printw("  username      : %s\n\n", USERNAME);

	printw("Downloading...\n");
	refresh();

	char saved_path[MAX_PATH_LEN + 1] = {0};
	int rc = download_from_server(HOST, PORT, USERNAME, PASSWORD,
	                              REMOTE_TARGET, DOWNLOAD_DIR, saved_path);

	printw("\nResult (rc = %d):\n", rc);
	print_err_bits(rc);

	if (rc == ERR_NONE) {
		printw("\n  Saved to: %s\n", saved_path);
		printw("  Comparing against original '%s'...\n", LOCAL_FILE);

		int cmp = files_identical(LOCAL_FILE, saved_path);
		if (cmp == 1)
			printw("  [OK] Files are identical.\n");
		else if (cmp == 0)
			printw("  [FAIL] Files differ – data was corrupted in transit.\n");
		else
			printw("  [WARN] Could not open one or both files for comparison.\n");
	}

	wait_key();
}

/**
 * test_list - Test list_directory().
 *
 * Lists LIST_PATH on the server and prints the raw returned string so the
 * caller can see exactly what list_directory() gives back.
 */
static void test_list(void)
{
	clear();
	printw("=== TEST 3: list_directory() ===\n\n");
	printw("  remote_path : %s\n", LIST_PATH);
	printw("  host        : %s:%s\n", HOST, PORT);
	printw("  username    : %s\n\n", USERNAME);

	printw("Listing...\n");
	refresh();

	char *listing = list_directory(HOST, PORT, USERNAME, PASSWORD, LIST_PATH);

	if (!listing) {
		printw("\n  [ERR] list_directory() returned NULL.\n");
		wait_key();
		return;
	}

	printw("\nRaw string returned by list_directory():\n\n");
	printw("%s\n", listing);

	free(listing);
	wait_key();
}

/* ── Entry point ───────────────────────────────────────────────────────── */

int main(void)
{
	initscr();
	cbreak();    /* Don't buffer keystrokes */
	noecho();    /* Don't echo typed characters */
	keypad(stdscr, TRUE);

	/* Run all three tests in sequence. */
	test_upload();
	test_download();
	test_list();

	/* Final summary screen. */
	clear();
	printw("=== All tests complete. ===\n\n");
	printw("Review the output above for any ERR_* flags.\n");
	printw("Downloaded files are in: %s\n\n", DOWNLOAD_DIR);
	printw("Press any key to exit...");
	refresh();
	getch();

	endwin();
	return 0;
}
