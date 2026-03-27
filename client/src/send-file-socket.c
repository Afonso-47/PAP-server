/**
 * send_file_socket.c
 *
 * C translation of send_file_socket.py.
 *
 * Provides client-side socket utilities for downloading files from,
 * uploading files to, and listing directories on a remote server using
 * a simple custom binary protocol.
 *
 * Protocol overview:
 *   1. Client sends UNLOCK_SIGNAL (0x01)
 *   2. Client sends username (4-byte big-endian length prefix + UTF-8 bytes)
 *   3. Client sends mode byte: 'D' (download), 'U' (upload), or 'L' (list)
 *   4. Mode-specific framing follows (see individual function docs)
 *
 * All strings returned by list_directory() are heap-allocated and must be
 * freed by the caller.
 *
 * Compile example (Linux/macOS):
 *   gcc -Wall -Wextra -o send_file_socket send_file_socket.c
 *
 * Windows note: Link against ws2_32 (-lws2_32) and call WSAStartup/WSACleanup
 * around program entry/exit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <shlobj.h>          /* SHGetFolderPathA */
  typedef SOCKET sock_t;
  #define CLOSE_SOCK(s) closesocket(s)
  #define SOCK_INVALID  INVALID_SOCKET
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <pwd.h>
  #include <sys/stat.h>
  typedef int sock_t;
  #define CLOSE_SOCK(s) close(s)
  #define SOCK_INVALID  (-1)
#endif

/* ── Protocol constants ──────────────────────────────────────────────────── */

#define BUFFER_SIZE     4096
#define UNLOCK_SIGNAL   "\x01"
#define MODE_DOWNLOAD   'D'
#define MODE_UPLOAD     'U'
#define MODE_LIST       'L'

/* Maximum length (bytes) of any path or filename sent over the wire. */
#define MAX_PATH_LEN    4096

/*
 * Bitmask error codes returned by the high-level API functions.
 *
 * Each protocol step owns one bit.  Multiple simultaneous failures are
 * reported by OR-ing the relevant bits together, so the caller can test
 * individual failures with e.g.:
 *
 *   int rc = upload_to_server(...);
 *   if (rc & ERR_UNLOCK) { ... }   // unlock step failed
 *   if (rc & ERR_PATH)   { ... }   // username path step failed
 *
 * The exact set of bits that each function may set is documented on that
 * function.  ERR_PATH_EXPAND and ERR_CONNECT are special: they are always
 * stand-alone (no further steps are attempted after them).
 */
#define ERR_NONE         0   /* Success                                    */
#define ERR_PATH_EXPAND  1   /* expand_path() failed before connecting     */
#define ERR_CONNECT      2   /* create_socket() / connect() failed         */
#define ERR_UNLOCK       4   /* send_unlock() failed                       */
#define ERR_PATH         8   /* send_path() for username failed            */
#define ERR_MODE        16   /* send_mode() failed                         */
#define ERR_REMOTE_PATH 32   /* send_path() for remote file/dir failed     */
#define ERR_TRANSFER    64   /* upload_file() / receive_file() failed      */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/**
 * make_dirs - Recursively create a directory and all intermediate directories.
 *
 * Equivalent to Python's os.makedirs(path, exist_ok=True).
 *
 * @param path  Directory path to create (modified in-place then restored).
 * @return      0 on success, -1 on error (errno is set).
 */
static int make_dirs(const char *path)
{
	char tmp[MAX_PATH_LEN];
	size_t len = strlen(path);

	if (len == 0 || len >= sizeof(tmp)) {
		errno = EINVAL;
		return -1;
	}

	strncpy(tmp, path, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';

	/* Remove trailing slash so we can iterate cleanly. */
	if (tmp[len - 1] == '/'
#ifdef _WIN32
		|| tmp[len - 1] == '\\'
#endif
	) {
		tmp[len - 1] = '\0';
	}

	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/'
#ifdef _WIN32
			|| *p == '\\'
#endif
		) {
			*p = '\0';
#ifdef _WIN32
			CreateDirectoryA(tmp, NULL);
#else
			mkdir(tmp, 0755);
#endif
			*p = '/';
		}
	}

#ifdef _WIN32
	if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
		return -1;
#else
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
		return -1;
#endif

	return 0;
}

/**
 * uint32_to_be - Encode a uint32 as a 4-byte big-endian buffer.
 *
 * @param value  Value to encode.
 * @param buf    Output buffer (must be at least 4 bytes).
 */
static void uint32_to_be(uint32_t value, unsigned char buf[4])
{
	buf[0] = (unsigned char)((value >> 24) & 0xFF);
	buf[1] = (unsigned char)((value >> 16) & 0xFF);
	buf[2] = (unsigned char)((value >>  8) & 0xFF);
	buf[3] = (unsigned char)( value        & 0xFF);
}

/**
 * be_to_uint32 - Decode a 4-byte big-endian buffer to uint32.
 *
 * @param buf  Input buffer (must be at least 4 bytes).
 * @return     Decoded value.
 */
static uint32_t be_to_uint32(const unsigned char buf[4])
{
	return ((uint32_t)buf[0] << 24)
		 | ((uint32_t)buf[1] << 16)
		 | ((uint32_t)buf[2] <<  8)
		 |  (uint32_t)buf[3];
}

/* ── Core socket primitives ──────────────────────────────────────────────── */

/**
 * create_socket - Open a TCP connection to host:port with a 10-second timeout.
 *
 * Uses getaddrinfo so both IPv4 and IPv6 addresses are resolved transparently.
 *
 * @param host  Hostname or IP address string.
 * @param port  Port number as a string (e.g. "9000").
 * @return      Connected socket descriptor, or SOCK_INVALID on error.
 */
sock_t create_socket(const char *host, const char *port)
{
	struct addrinfo hints, *res, *rp;
	sock_t fd = SOCK_INVALID;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;    /* Accept both IPv4 and IPv6 */
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, port, &hints, &res) != 0)
		return SOCK_INVALID;

	for (rp = res; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd == SOCK_INVALID)
			continue;

		/* Set a 10-second receive/send timeout. */
#ifdef _WIN32
		DWORD tv = 10000;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
		struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

		if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0)
			break;          /* Success */

		CLOSE_SOCK(fd);
		fd = SOCK_INVALID;
	}

	freeaddrinfo(res);
	return fd;
}

/**
 * recv_exact - Receive exactly `length` bytes from the socket.
 *
 * Loops internally until all bytes arrive or the connection is lost.
 *
 * @param sock    Connected socket.
 * @param buf     Output buffer (must be at least `length` bytes).
 * @param length  Number of bytes to receive.
 * @return        0 on success, -1 if the connection closed prematurely.
 */
int recv_exact(sock_t sock, unsigned char *buf, size_t length)
{
	size_t received = 0;
	while (received < length) {
#ifdef _WIN32
		int n = recv(sock, (char *)(buf + received), (int)(length - received), 0);
#else
		ssize_t n = recv(sock, buf + received, length - received, 0);
#endif
		if (n <= 0)
			return -1;   /* Connection closed or error */
		received += (size_t)n;
	}
	return 0;
}

/**
 * send_all - Send all `length` bytes from buf, retrying on short sends.
 *
 * @param sock    Connected socket.
 * @param buf     Data to send.
 * @param length  Number of bytes to send.
 * @return        0 on success, -1 on error.
 */
static int send_all(sock_t sock, const unsigned char *buf, size_t length)
{
	size_t sent = 0;
	while (sent < length) {
#ifdef _WIN32
		int n = send(sock, (const char *)(buf + sent), (int)(length - sent), 0);
#else
		ssize_t n = send(sock, buf + sent, length - sent, 0);
#endif
		if (n <= 0)
			return -1;
		sent += (size_t)n;
	}
	return 0;
}

/* ── Protocol framing helpers ────────────────────────────────────────────── */

/**
 * send_unlock - Send the unlock signal byte (0x01) to the server.
 *
 * The server requires this as the first byte of every connection to verify
 * the client is speaking the expected protocol.
 *
 * @param sock  Connected socket.
 * @return      0 on success, -1 on error.
 */
int send_unlock(sock_t sock)
{
	const unsigned char sig = 0x01;
	return send_all(sock, &sig, 1);
}

/**
 * send_mode - Send a single mode byte to the server.
 *
 * Valid modes: MODE_DOWNLOAD ('D'), MODE_UPLOAD ('U'), MODE_LIST ('L').
 *
 * @param sock  Connected socket.
 * @param mode  One of the MODE_* constants.
 * @return      0 on success, -1 on error.
 */
int send_mode(sock_t sock, char mode)
{
	return send_all(sock, (const unsigned char *)&mode, 1);
}

/**
 * send_path - Send a length-prefixed path string to the server.
 *
 * Wire format: [4 bytes big-endian length][UTF-8 path bytes]
 *
 * The path must be between 1 and MAX_PATH_LEN bytes.
 *
 * @param sock  Connected socket.
 * @param path  Null-terminated path string to send.
 * @return      0 on success, -1 on validation or send error.
 */
int send_path(sock_t sock, const char *path)
{
	size_t path_len = strlen(path);

	if (path_len == 0 || path_len > MAX_PATH_LEN) {
		fprintf(stderr, "send_path: invalid path length %zu\n", path_len);
		return -1;
	}

	unsigned char len_buf[4];
	uint32_to_be((uint32_t)path_len, len_buf);

	if (send_all(sock, len_buf, 4) != 0)
		return -1;

	return send_all(sock, (const unsigned char *)path, path_len);
}

/* ── File transfer ───────────────────────────────────────────────────────── */

/**
 * receive_file - Receive a file from the server and write it to output_dir.
 *
 * Wire format received from server:
 *   [1 byte status: 0x00 = OK]
 *   [4 bytes big-endian filename length]
 *   [filename bytes]
 *   [raw file data until connection close]
 *
 * @param sock        Connected socket, positioned after the mode byte.
 * @param output_dir  Local directory to write the file into.
 * @param out_path    Buffer to store the resulting file path (at least
 *                    MAX_PATH_LEN + 1 bytes).
 * @return            0 on success, -1 on error.
 */
int receive_file(sock_t sock, const char *output_dir, char *out_path)
{
	unsigned char status;
	if (recv_exact(sock, &status, 1) != 0 || status != 0x00) {
		fprintf(stderr, "receive_file: server reported error\n");
		return -1;
	}

	unsigned char len_buf[4];
	if (recv_exact(sock, len_buf, 4) != 0) {
		fprintf(stderr, "receive_file: failed to read filename length\n");
		return -1;
	}
	uint32_t name_len = be_to_uint32(len_buf);
	if (name_len == 0 || name_len > MAX_PATH_LEN) {
		fprintf(stderr, "receive_file: invalid filename length %u\n", name_len);
		return -1;
	}

	char filename[MAX_PATH_LEN + 1];
	if (recv_exact(sock, (unsigned char *)filename, name_len) != 0) {
		fprintf(stderr, "receive_file: failed to read filename\n");
		return -1;
	}
	filename[name_len] = '\0';

	if (make_dirs(output_dir) != 0) {
		fprintf(stderr, "receive_file: cannot create output directory '%s'\n", output_dir);
		return -1;
	}

	snprintf(out_path, MAX_PATH_LEN, "%s/%s", output_dir, filename);

	FILE *fp = fopen(out_path, "wb");
	if (!fp) {
		fprintf(stderr, "receive_file: cannot open '%s' for writing\n", out_path);
		return -1;
	}

	unsigned char buf[BUFFER_SIZE];
	while (1) {
#ifdef _WIN32
		int n = recv(sock, (char *)buf, sizeof(buf), 0);
#else
		ssize_t n = recv(sock, buf, sizeof(buf), 0);
#endif
		if (n <= 0)
			break;   /* Connection closed – end of file data */
		fwrite(buf, 1, (size_t)n, fp);
	}

	fclose(fp);
	return 0;
}

/**
 * upload_file - Upload a local file to the server.
 *
 * Wire format sent to server:
 *   [length-prefixed target_path]   → via send_path()
 *
 * Wire format received from server:
 *   [1 byte status: 0x00 = OK]
 *
 * After the status, raw file bytes are streamed until EOF.
 *
 * @param sock         Connected socket, positioned after the mode byte.
 * @param filepath     Local file to upload (must exist).
 * @param target_path  Destination path on the server; if NULL, defaults to
 *                     the basename of filepath.
 * @return             0 on success, -1 on error.
 */
int upload_file(sock_t sock, const char *filepath, const char *target_path)
{
	/* Validate local file. */
	FILE *fp = fopen(filepath, "rb");
	if (!fp) {
		fprintf(stderr, "upload_file: cannot open '%s'\n", filepath);
		return -1;
	}

	/* Resolve target path. */
	char default_target[MAX_PATH_LEN];
	if (!target_path) {
		/* Use basename: find last '/' or '\' */
		const char *base = strrchr(filepath, '/');
#ifdef _WIN32
		const char *base2 = strrchr(filepath, '\\');
		if (!base || (base2 && base2 > base))
			base = base2;
#endif
		strncpy(default_target, base ? base + 1 : filepath, sizeof(default_target) - 1);
		default_target[sizeof(default_target) - 1] = '\0';
		target_path = default_target;
	}

	if (send_path(sock, target_path) != 0) {
		fclose(fp);
		return -1;
	}

	/* Wait for server acknowledgement. */
	unsigned char status;
	if (recv_exact(sock, &status, 1) != 0 || status != 0x00) {
		fprintf(stderr, "upload_file: server refused write to '%s'\n", target_path);
		fclose(fp);
		return -1;
	}

	/* Stream file data. */
	unsigned char buf[BUFFER_SIZE];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (send_all(sock, buf, n) != 0) {
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);
	return 0;
}

/* ── Directory listing ───────────────────────────────────────────────────── */

/**
 * list_directory_sock - Retrieve a directory listing from the server.
 *
 * Wire format received:
 *   [1 byte status: 0x00 = OK]
 *   Repeated until end-of-list:
 *     [4 bytes big-endian entry length]
 *     [entry bytes]
 *   End-of-list marker: [4 bytes = 0x00000000]
 *
 * @param sock         Connected socket, positioned after the mode byte.
 * @param remote_path  Directory path on the server to list.
 * @return             Heap-allocated string with one entry per line, or NULL
 *                     on error. The caller must free() the returned string.
 */
char *list_directory_sock(sock_t sock, const char *remote_path)
{
	if (send_path(sock, remote_path) != 0)
		return NULL;

	unsigned char status;
	if (recv_exact(sock, &status, 1) != 0 || status != 0x00) {
		fprintf(stderr, "list_directory_sock: server reported error\n");
		return NULL;
	}

	/* Dynamic string accumulator. */
	size_t buf_cap  = 4096;
	size_t buf_used = 0;
	char  *result   = malloc(buf_cap);
	if (!result)
		return NULL;
	result[0] = '\0';

	char entry[MAX_PATH_LEN + 1];

	while (1) {
		unsigned char len_buf[4];
		if (recv_exact(sock, len_buf, 4) != 0) {
			free(result);
			return NULL;
		}
		uint32_t entry_len = be_to_uint32(len_buf);

		if (entry_len == 0)
			break;   /* End-of-list marker */

		if (entry_len > MAX_PATH_LEN) {
			fprintf(stderr, "list_directory_sock: invalid entry length %u\n", entry_len);
			free(result);
			return NULL;
		}

		if (recv_exact(sock, (unsigned char *)entry, entry_len) != 0) {
			free(result);
			return NULL;
		}
		entry[entry_len] = '\0';

		/* Grow buffer if needed: +1 for newline, +1 for NUL. */
		size_t needed = buf_used + entry_len + 2;
		if (needed > buf_cap) {
			buf_cap = needed * 2;
			char *tmp = realloc(result, buf_cap);
			if (!tmp) {
				free(result);
				return NULL;
			}
			result = tmp;
		}

		if (buf_used > 0)
			result[buf_used++] = '\n';

		memcpy(result + buf_used, entry, entry_len);
		buf_used += entry_len;
		result[buf_used] = '\0';
	}

	return result;   /* Caller must free() */
}

/* ── Path utilities ──────────────────────────────────────────────────────── */

/**
 * get_default_download_dir - Return the platform default download directory.
 *
 * On Windows: %USERPROFILE%\Downloads (created if absent).
 * On Linux/macOS: "." (current working directory).
 *
 * @param buf      Output buffer to store the path string.
 * @param buf_size Size of buf in bytes.
 * @return         Pointer to buf on success, NULL on error.
 */
char *get_default_download_dir(char *buf, size_t buf_size)
{
#ifdef _WIN32
	char home[MAX_PATH];
	if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home) != S_OK)
		return NULL;
	snprintf(buf, buf_size, "%s\\Downloads", home);
	CreateDirectoryA(buf, NULL);   /* Silently OK if it already exists */
#else
	strncpy(buf, ".", buf_size - 1);
	buf[buf_size - 1] = '\0';
#endif
	return buf;
}

/**
 * expand_path - Expand a leading tilde (~) in a path to the home directory.
 *
 * Handles:
 *   ~/path           → $HOME/path
 *   ~username/path   → home directory of `username` + /path  (POSIX only)
 *   /absolute/path   → unchanged
 *   relative/path    → unchanged
 *
 * @param path     Input path string.
 * @param out      Output buffer.
 * @param out_size Size of out in bytes.
 * @return         Pointer to out on success, NULL on error.
 */
char *expand_path(const char *path, char *out, size_t out_size)
{
	if (!path || path[0] == '\0') {
		if (out_size > 0) out[0] = '\0';
		return out;
	}

	if (path[0] != '~') {
		strncpy(out, path, out_size - 1);
		out[out_size - 1] = '\0';
		return out;
	}

	/* Find where the tilde-prefix ends. */
	const char *sep = strchr(path, '/');
	size_t user_len = sep ? (size_t)(sep - path - 1) : strlen(path) - 1;

#ifdef _WIN32
	/* Windows: only bare ~ is supported. */
	if (user_len == 0) {
		char profile[MAX_PATH];
		if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, profile) != S_OK)
			return NULL;
		snprintf(out, out_size, "%s%s", profile, sep ? sep : "");
		return out;
	}
	/* ~username not supported on Windows – return as-is. */
	strncpy(out, path, out_size - 1);
	out[out_size - 1] = '\0';
	return out;
#else
	if (user_len == 0) {
		/* ~/... — use $HOME or getpwuid. */
		const char *home = getenv("HOME");
		if (!home) {
			struct passwd *pw = getpwuid(getuid());
			if (!pw) return NULL;
			home = pw->pw_dir;
		}
		snprintf(out, out_size, "%s%s", home, sep ? sep : "");
	} else {
		/* ~username/... */
		char username[256];
		if (user_len >= sizeof(username)) return NULL;
		strncpy(username, path + 1, user_len);
		username[user_len] = '\0';

		struct passwd *pw = getpwnam(username);
		if (!pw) return NULL;
		snprintf(out, out_size, "%s%s", pw->pw_dir, sep ? sep : "");
	}
	return out;
#endif
}

/* ── High-level API ──────────────────────────────────────────────────────── */

/**
 * download_from_server - Download a file from the remote server.
 *
 * Connects, performs the unlock/username/mode handshake, sends the remote
 * path, then saves the received file under output_dir.
 *
 * @param host        Server hostname or IP address.
 * @param port        Server port as a string (e.g. "9000").
 * @param username    Username sent to the server for tilde expansion.
 * @param remote_path Path to the file on the server.
 * @param output_dir  Local directory to save the file into (tilde expanded).
 * @param out_path    Buffer (≥ MAX_PATH_LEN + 1 bytes) filled with the saved
 *                    file's local path on success.
 * @return            ERR_NONE (0) on success, or a bitmask of one or more
 *                    ERR_* constants indicating which steps failed:
 *                      ERR_PATH_EXPAND  – output_dir tilde expansion failed
 *                      ERR_CONNECT      – could not connect to host:port
 *                      ERR_UNLOCK       – send_unlock() failed
 *                      ERR_PATH         – sending username failed
 *                      ERR_MODE         – sending mode byte failed
 *                      ERR_REMOTE_PATH  – sending remote_path failed
 *                      ERR_TRANSFER     – receiving / writing the file failed
 */
int download_from_server(const char *host,
						 const char *port,
						 const char *username,
						 const char *remote_path,
						 const char *output_dir,
						 char       *out_path)
{
	char expanded_dir[MAX_PATH_LEN + 1];
	if (!expand_path(output_dir, expanded_dir, sizeof(expanded_dir)))
		return ERR_PATH_EXPAND;

	sock_t sock = create_socket(host, port);
	if (sock == SOCK_INVALID)
		return ERR_CONNECT;

	int rc = ERR_NONE;
	if (send_unlock(sock) != 0)
		rc |= ERR_UNLOCK;
	if (send_path(sock, username) != 0)
		rc |= ERR_PATH;
	if (send_mode(sock, MODE_DOWNLOAD) != 0)
		rc |= ERR_MODE;
	if (send_path(sock, remote_path) != 0)
		rc |= ERR_REMOTE_PATH;
	if (receive_file(sock, expanded_dir, out_path) != 0)
		rc |= ERR_TRANSFER;

	CLOSE_SOCK(sock);
	return rc;
}

/**
 * upload_to_server - Upload a local file to the remote server.
 *
 * Connects, performs the unlock/username/mode handshake, then streams the
 * file to the server.
 *
 * @param host          Server hostname or IP address.
 * @param port          Server port as a string (e.g. "9000").
 * @param username      Username sent to the server for tilde expansion.
 * @param local_file    Path to the local file (tilde expanded before use).
 * @param remote_target Destination path on server; if NULL, defaults to the
 *                      basename of local_file.
 * @return              ERR_NONE (0) on success, or a bitmask of one or more
 *                      ERR_* constants indicating which steps failed:
 *                        ERR_PATH_EXPAND  – local_file tilde expansion failed
 *                        ERR_CONNECT      – could not connect to host:port
 *                        ERR_UNLOCK       – send_unlock() failed
 *                        ERR_PATH         – sending username failed
 *                        ERR_MODE         – sending mode byte failed
 *                        ERR_TRANSFER     – upload_file() failed
 */
int upload_to_server(const char *host,
					 const char *port,
					 const char *username,
					 const char *local_file,
					 const char *remote_target)
{
	char expanded_file[MAX_PATH_LEN + 1];
	if (!expand_path(local_file, expanded_file, sizeof(expanded_file)))
		return ERR_PATH_EXPAND;

	sock_t sock = create_socket(host, port);
	if (sock == SOCK_INVALID)
		return ERR_CONNECT;

	int rc = ERR_NONE;
	if (send_unlock(sock) != 0)
		rc |= ERR_UNLOCK;
	if (send_path(sock, username) != 0)
		rc |= ERR_PATH;
	if (send_mode(sock, MODE_UPLOAD) != 0)
		rc |= ERR_MODE;
	if (upload_file(sock, expanded_file, remote_target) != 0)
		rc |= ERR_TRANSFER;

	CLOSE_SOCK(sock);
	return rc;
}

/**
 * list_directory - List contents of a remote directory.
 *
 * Connects, performs the unlock/username/mode handshake, then retrieves the
 * directory listing as a newline-separated string.
 *
 * Note: because this function returns a heap-allocated string rather than an
 * int, it cannot use the ERR_* bitmask scheme.  NULL indicates failure;
 * a non-NULL pointer (possibly pointing to an empty string "") indicates
 * success.  The caller is responsible for free()-ing the returned pointer.
 *
 * @param host        Server hostname or IP address.
 * @param port        Server port as a string (e.g. "9000").
 * @param username    Username sent to the server for tilde expansion.
 * @param remote_path Path to the directory on the server.
 * @return            Heap-allocated string (entries separated by '\n'), or
 *                    NULL on error.
 */
char *list_directory(const char *host,
					 const char *port,
					 const char *username,
					 const char *remote_path)
{
	sock_t sock = create_socket(host, port);
	if (sock == SOCK_INVALID)
		return NULL;

	char *result = NULL;
	if (send_unlock(sock)           == 0 &&
		send_path(sock, username)   == 0 &&
		send_mode(sock, MODE_LIST)  == 0)
	{
		result = list_directory_sock(sock, remote_path);
	}

	CLOSE_SOCK(sock);
	return result;   /* Caller must free() */
}
