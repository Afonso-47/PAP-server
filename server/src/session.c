
/**
 * @file session.c
 * @brief Session handler for PAP server bidirectional file transfer
 *
 * This module implements authenticated file transfer sessions, supporting:
 * - User authentication via username
 * - Download mode (server → client)
 * - Upload mode (client → server)
 * - Directory listing mode
 * - Tilde (~) path expansion using system user database
 * - Automatic parent directory creation for uploads
 * - Status byte error reporting (0x00=OK, 0x01=ERROR)
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1   /* Expose DT_DIR / DT_REG / d_type from <dirent.h> */
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <wordexp.h>
#include <dirent.h>
#include <limits.h>
#include <shadow.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "session.h"

/* ========== Protocol Constants ========== */
#define BUFFER_SIZE 4096          /**< Size of file transfer buffer */
#define MODE_DOWNLOAD 'D'         /**< Download mode: server → client */
#define MODE_UPLOAD   'U'         /**< Upload mode: client → server */
#define MODE_LIST     'L'         /**< List mode: directory listing */
#define STATUS_OK 0x00            /**< Status byte: operation succeeded */
#define STATUS_ERROR 0x01         /**< Status byte: operation failed */

/**
 * @brief Current authenticated username for tilde expansion
 *
 * This global stores the username sent by the client during authentication.
 * It is used by expand_tilde() to resolve ~ and ~user/ paths.
 */
static char current_username[256] = {0};

/* ========== Low-Level Socket Helpers ========== */

/**
 * @brief Receive an exact number of bytes from a socket
 *
 * This function loops until exactly 'len' bytes have been received,
 * handling partial reads that can occur with TCP sockets.
 *
 * @param fd Socket file descriptor
 * @param buf Buffer to store received data
 * @param len Number of bytes to receive
 * @return Total bytes received (should equal len), or <= 0 on error/disconnect
 */
static int recv_exact(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        int n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return n;  // Error or connection closed
        total += n;
    }
    return (int)total;
}

/**
 * @brief Receive a length-prefixed string (path/username) from socket
 *
 * Protocol format:
 * - 4 bytes: big-endian uint32_t length
 * - N bytes: UTF-8 string data (not null-terminated on wire)
 *
 * @param fd Socket file descriptor
 * @return Malloc'd null-terminated string, or NULL on error
 * @note Caller must free() the returned string
 * @note Rejects lengths > 4096 to prevent memory exhaustion attacks
 */
static char *recv_path_alloc(int fd) {
    uint32_t len_be;
    int n = recv_exact(fd, &len_be, sizeof(len_be));
    if (n <= 0) return NULL;

    // Convert from network byte order (big-endian) to host byte order
    uint32_t len = ntohl(len_be);
    if (len == 0 || len > 4096) return NULL;  // Sanity check

    char *path = malloc(len + 1);  // +1 for null terminator
    if (!path) return NULL;

    n = recv_exact(fd, path, len);
    if (n <= 0) { free(path); return NULL; }
    path[len] = '\0';  // Add null terminator
    return path;
}

/**
 * @brief Send an exact number of bytes to a socket
 *
 * This function loops until exactly 'len' bytes have been sent,
 * handling partial writes that can occur with TCP sockets.
 *
 * @param fd Socket file descriptor
 * @param buf Buffer containing data to send
 * @param len Number of bytes to send
 * @return Total bytes sent (should equal len), or <= 0 on error
 */
static int send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < len) {
        int n = send(fd, p + total, len - total, 0);
        if (n <= 0) return n;  // Error or connection closed
        total += n;
    }
    return (int)total;
}

/**
 * @brief Send a length-prefixed string to socket (4-byte BE length + bytes)
 */
static int send_path_raw(int fd, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    if (len == 0 || len > 4096) return -1;

    uint32_t len_be = htonl(len);
    if (send_all(fd, &len_be, sizeof(len_be)) <= 0) return -1;
    if (send_all(fd, s, len) <= 0) return -1;
    return 0;
}

/* ========== Path Manipulation Utilities ========== */

/**
 * @brief Extract the filename (basename) from a path
 *
 * Returns a pointer to the last component of the path after the final '/'.
 * If no '/' is present, returns the entire path.
 *
 * @param path Full file path
 * @return Pointer to basename within the same string (not a copy)
 *
 * @example
 *   path_basename("/home/user/file.txt") → "file.txt"
 *   path_basename("file.txt") → "file.txt"
 */
static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/**
 * @brief Expand tilde (~) in paths to actual home directories
 *
 * Supports three formats:
 * - "~/path" → authenticated user's home + "/path"
 * - "~username/path" → specified user's home + "/path"
 * - Paths without ~ → returned as-is (duplicated)
 *
 * Resolution order for "~/":
 * 1. Authenticated user (current_username) via getpwnam()
 * 2. $HOME environment variable
 * 3. Fallback to "/root"
 *
 * @param path Path to expand
 * @return Malloc'd expanded path, or duplicate of original on failure
 * @note Caller must free() the returned string
 * @note Uses system password database (pwd.h) for user lookup
 */
static char *expand_tilde(const char *path) {
    // No tilde? Return a copy unchanged
    if (!path || path[0] != '~') {
        return strdup(path);
    }

    const char *home = NULL;
    const char *rest = path + 1;  // Skip the '~'

    // Case 1: "~" or "~/..." (authenticated user's home)
    if (rest[0] == '/' || rest[0] == '\0') {
        // Try authenticated user first
        if (current_username[0] != '\0') {
            struct passwd *pw = getpwnam(current_username);
            if (pw) {
                home = pw->pw_dir;
            }
        }
        // Fallback to $HOME environment variable
        if (!home) {
            home = getenv("HOME");
        }
        // Last resort: assume root
        if (!home) {
            home = "/root";
        }
    }
    // Case 2: "~username/..." (specific user's home)
    else {
        const char *slash = strchr(rest, '/');
        size_t userlen = slash ? (size_t)(slash - rest) : strlen(rest);
        char username[256];
        if (userlen < sizeof(username)) {
            memcpy(username, rest, userlen);
            username[userlen] = '\0';
            struct passwd *pw = getpwnam(username);
            if (pw) {
                home = pw->pw_dir;
                rest = slash ? slash : "";  // Point to remainder after username
            }
        }
    }

    // If expansion failed, return original path as copy
    if (!home) {
        return strdup(path);
    }

    // Concatenate home + rest
    size_t homelen = strlen(home);
    size_t restlen = strlen(rest);
    char *expanded = malloc(homelen + restlen + 1);
    if (!expanded) return NULL;

    memcpy(expanded, home, homelen);
    memcpy(expanded + homelen, rest, restlen + 1);  // Include null terminator
    return expanded;
}

/**
 * @brief Recursively create all parent directories for a given path
 *
 * Splits the path at each '/' and creates directories incrementally.
 * Ignores EEXIST errors (directory already exists).
 * Used during uploads to ensure target directories exist.
 *
 * @param path Full file path whose parents should be created
 * @return 0 on success, -1 on error (sets errno)
 *
 * @example
 *   ensure_parent_dirs("/a/b/c/file.txt") creates /a, /a/b, /a/b/c
 *
 * @note Does not create the final component (assumes it's a file)
 * @note Directories are created with mode 0755 (rwxr-xr-x)
 */
static int ensure_parent_dirs(const char *path) {
    char tmp[4096 + 1];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;  // Path too long
    memcpy(tmp, path, len + 1);
    
    // Walk through path, creating each directory component
    for (size_t i = 1; i < len; ++i) {  // Start at 1 to skip leading '/'
        if (tmp[i] == '/') {
            tmp[i] = '\0';  // Temporarily terminate string
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                return -1;  // mkdir failed for reason other than "already exists"
            }
            tmp[i] = '/';  // Restore slash
        }
    }
    return 0;
}

/**
 * @brief Get current authenticated user's home directory and root status
 */
static int get_user_home(char *home_out, size_t out_size, int *is_root_out) {
    struct passwd *pw = getpwnam(current_username);
    if (!pw || !pw->pw_dir) return -1;

    if (strlen(pw->pw_dir) >= out_size) return -1;
    strcpy(home_out, pw->pw_dir);
    *is_root_out = (pw->pw_uid == 0) ? 1 : 0;
    return 0;
}

/**
 * @brief Check whether path is equal to or inside base directory
 */
static int path_is_within(const char *path, const char *base) {
    size_t base_len = strlen(base);
    if (strncmp(path, base, base_len) != 0) return 0;
    return path[base_len] == '\0' || path[base_len] == '/';
}

/**
 * @brief Resolve the closest existing ancestor of a path
 */
static int resolve_existing_ancestor(const char *path, char *resolved, size_t resolved_size) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    while (1) {
        char *rp = realpath(tmp, resolved);
        if (rp) {
            if (strlen(resolved) >= resolved_size) return -1;
            return 0;
        }

        char *slash = strrchr(tmp, '/');
        if (!slash) return -1;

        if (slash == tmp) {
            strcpy(tmp, "/");
        } else {
            *slash = '\0';
        }
    }
}

/**
 * @brief Enforce per-user path policy (non-root users restricted to home)
 */
static int enforce_user_path_policy(const char *expanded_path, int for_upload) {
    char user_home[PATH_MAX];
    int is_root = 0;

    if (get_user_home(user_home, sizeof(user_home), &is_root) != 0) {
        return -1;
    }
    if (is_root) {
        return 0;
    }

    if (expanded_path[0] != '/') {
        return -1;
    }

    char resolved_home[PATH_MAX];
    if (!realpath(user_home, resolved_home)) {
        return -1;
    }

    char resolved_target[PATH_MAX];
    int ok;
    if (for_upload) {
        if (resolve_existing_ancestor(expanded_path, resolved_target, sizeof(resolved_target)) != 0) {
            return -1;
        }
        ok = path_is_within(resolved_target, resolved_home);
    } else {
        if (!realpath(expanded_path, resolved_target)) {
            return -1;
        }
        ok = path_is_within(resolved_target, resolved_home);
    }

    return ok ? 0 : -1;
}

/**
 * @brief Extract crypt setting (algorithm+salt) from shadow hash
 */
static int extract_crypt_setting(const char *stored_hash, char *out, size_t out_size) {
    if (!stored_hash || stored_hash[0] == '\0') return -1;

    // Modern modular format: $id$[params$]salt$hash
    if (stored_hash[0] == '$') {
        const char *last_dollar = strrchr(stored_hash, '$');
        if (!last_dollar || last_dollar == stored_hash) return -1;

        size_t setting_len = (size_t)(last_dollar - stored_hash) + 1; // include trailing '$'
        if (setting_len >= out_size) return -1;
        memcpy(out, stored_hash, setting_len);
        out[setting_len] = '\0';
        return 0;
    }

    // Legacy DES format uses first two chars as salt
    if (strlen(stored_hash) < 2 || out_size < 3) return -1;
    out[0] = stored_hash[0];
    out[1] = stored_hash[1];
    out[2] = '\0';
    return 0;
}

/**
 * @brief Authenticate current user with password hash response
 */
static int authenticate_user(int client_fd) {
    struct spwd *sp = getspnam(current_username);
    if (!sp || !sp->sp_pwdp || sp->sp_pwdp[0] == '\0' ||
        sp->sp_pwdp[0] == '!' || sp->sp_pwdp[0] == '*') {
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    char setting[512];
    if (extract_crypt_setting(sp->sp_pwdp, setting, sizeof(setting)) != 0) {
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    if (send_path_raw(client_fd, setting) != 0) {
        return -1;
    }

    char *client_hash = recv_path_alloc(client_fd);
    if (!client_hash) {
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    int ok = (strcmp(client_hash, sp->sp_pwdp) == 0);
    free(client_hash);

    unsigned char status = ok ? STATUS_OK : STATUS_ERROR;
    if (send_all(client_fd, &status, 1) <= 0) {
        return -1;
    }

    return ok ? 0 : -1;
}

/* ========== Protocol Mode Handlers ========== */

/**
 * @brief Handle DOWNLOAD mode (server → client)
 *
 * Protocol flow:
 * 1. Receive requested file path from client
 * 2. Expand tilde (~) in path
 * 3. Open file for reading
 * 4. Send STATUS_OK byte
 * 5. Send filename length (4-byte big-endian) + filename string
 * 6. Stream file contents in BUFFER_SIZE chunks until EOF
 *
 * @param client_fd Connected client socket
 * @return 0 on success, -1 on error
 *
 * @note Sends STATUS_ERROR and closes on failure (file not found, etc.)
 * @note Only sends the basename of the file, not the full path
 */
static int handle_download(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    // Step 1: Receive the file path client wants to download
    char *requested_path = recv_path_alloc(client_fd);
    if (!requested_path) {
        printf("Invalid or missing requested path.\n");
        return -1;
    }

    // Step 2: Expand tilde (~) to actual home directory
    char *expanded_path = expand_tilde(requested_path);
    free(requested_path);
    if (!expanded_path) {
        printf("Path expansion failed.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    if (enforce_user_path_policy(expanded_path, 0) != 0) {
        printf("Access denied for user '%s': %s\n", current_username, expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    // Extract the filename (basename) for sending to client
    const char *name = path_basename(expanded_path);
    uint32_t name_len = (uint32_t)strlen(name);
    
    // CRITICAL: Copy filename before freeing expanded_path!
    // The 'name' pointer points into 'expanded_path' memory,
    // so we must copy it before free() to avoid use-after-free.
    char name_copy[4096];
    if (name_len >= sizeof(name_copy)) {
        printf("Filename too long.\n");
        free(expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }
    memcpy(name_copy, name, name_len + 1);

    // Step 3: Open the file for binary reading
    FILE *f = fopen(expanded_path, "rb");
    if (!f) {
        perror("fopen");
        printf("Hint: ensure requested file exists: %s\n", expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    printf("Sending file to client: %s\n", expanded_path);
    free(expanded_path);  // No longer needed after opening file

    // Step 4: Send STATUS_OK to indicate file was opened successfully
    unsigned char status = STATUS_OK;
    if (send_all(client_fd, &status, 1) <= 0) {
        perror("send status");
        fclose(f);
        return -1;
    }

    // Step 5: Send filename length (big-endian) and filename string
    uint32_t name_len_be = htonl(name_len);
    if (send_all(client_fd, &name_len_be, sizeof(name_len_be)) <= 0) {
        perror("send filename length");
        fclose(f);
        return -1;
    }
    if (send_all(client_fd, name_copy, name_len) <= 0) {
        perror("send filename");
        fclose(f);
        return -1;
    }

    // Step 6: Stream file contents in chunks until EOF
    size_t nread;
    while ((nread = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        if (send_all(client_fd, buffer, nread) <= 0) {
            perror("send file data");
            fclose(f);
            return -1;
        }
    }

    // Check if loop ended due to error (not just EOF)
    if (ferror(f)) {
        perror("fread");
        fclose(f);
        return -1;
    }

    fclose(f);
    printf("File sent.\n");
    return 0;
}

/**
 * @brief Handle UPLOAD mode (client → server)
 *
 * Protocol flow:
 * 1. Receive target file path from client
 * 2. Expand tilde (~) in path
 * 3. Create parent directories if needed
 * 4. Open file for writing
 * 5. Send STATUS_OK byte
 * 6. Receive file contents in chunks until connection closes
 * 7. Write received data to file
 *
 * @param client_fd Connected client socket
 * @return 0 on success, -1 on error
 *
 * @note Sends STATUS_ERROR and closes on failure (permission denied, etc.)
 * @note Automatically creates parent directories with mode 0755
 * @note Overwrites existing files without warning
 */
static int handle_upload(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    // Step 1: Receive target path where file should be saved
    char *target_path = recv_path_alloc(client_fd);
    if (!target_path) {
        printf("Invalid or missing target path.\n");
        return -1;
    }

    // Step 2: Expand tilde (~) to actual home directory
    char *expanded_path = expand_tilde(target_path);
    free(target_path);
    if (!expanded_path) {
        printf("Path expansion failed.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    if (enforce_user_path_policy(expanded_path, 1) != 0) {
        printf("Access denied for user '%s': %s\n", current_username, expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    // Step 3: Create parent directories if they don't exist
    if (ensure_parent_dirs(expanded_path) != 0) {
        perror("mkdir");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    printf("Receiving file for path: %s\n", expanded_path);

    // Step 4: Open file for binary writing (overwrites existing file)
    FILE *f = fopen(expanded_path, "wb");
    if (!f) {
        perror("fopen");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    // Step 5: Send STATUS_OK to tell client we're ready to receive
    unsigned char status = STATUS_OK;
    send_all(client_fd, &status, 1);
    free(expanded_path);  // No longer needed

    // Step 6: Receive file data in chunks until connection closes
    int n;
    while ((n = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        // Step 7: Write received chunk to disk
        if (fwrite(buffer, 1, n, f) != (size_t)n) {
            perror("fwrite");
            fclose(f);
            return -1;
        }
    }

    // Check if loop ended due to error (not just connection close)
    if (n < 0) {
        perror("recv file data");
    }

    fclose(f);
    if (n < 0) {
        return -1;
    }

    printf("File saved.\n");
    return 0;
}

/**
 * @brief Handle LIST mode (directory listing)
 *
 * Protocol flow:
 * 1. Receive directory path from client
 * 2. Expand tilde (~) in path
 * 3. Open directory for reading
 * 4. Send STATUS_OK byte
 * 5. For each entry (excluding "." and ".."):
 *    - Send entry type byte (LIST_TYPE_DIR or LIST_TYPE_FILE)
 *    - Send entry name length (4-byte big-endian)
 *    - Send entry name string
 * 6. Send end-of-list marker: type byte LIST_TYPE_EOL (0x00)
 *
 * @param client_fd Connected client socket
 * @return 0 on success, -1 on error
 *
 * @note Sends STATUS_ERROR and closes on failure (permission denied, not a directory, etc.)
 * @note Excludes "." and ".." entries from listing
 * @note End-of-list is signaled by a LIST_TYPE_EOL (0x00) type byte with no following data
 * @note Entries whose d_type is DT_UNKNOWN are treated as LIST_TYPE_FILE
 */

/** Type byte values for directory listing entries. */
#define LIST_TYPE_EOL  0x00   /**< End-of-list marker (no length/name follows) */
#define LIST_TYPE_FILE 0x01   /**< Regular file (or unknown type)              */
#define LIST_TYPE_DIR  0x02   /**< Directory                                   */

static int handle_list(int client_fd) {
	// Step 1: Receive directory path to list
	char *dir_path = recv_path_alloc(client_fd);
	if (!dir_path) {
		unsigned char status = STATUS_ERROR;
		send_all(client_fd, &status, 1);
		return -1;
	}

	// Step 2: Expand tilde (~) to actual home directory
	char *expanded_path = expand_tilde(dir_path);
	free(dir_path);
	if (!expanded_path) {
		unsigned char status = STATUS_ERROR;
		send_all(client_fd, &status, 1);
		return -1;
	}

    if (enforce_user_path_policy(expanded_path, 0) != 0) {
        printf("Access denied for user '%s': %s\n", current_username, expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

	// Step 3: Open directory for reading
	DIR *dir = opendir(expanded_path);
	if (!dir) {
		perror("opendir");
		unsigned char status = STATUS_ERROR;
		send_all(client_fd, &status, 1);
		free(expanded_path);
		return -1;
	}

	printf("Listing directory: %s\n", expanded_path);
	free(expanded_path);

	// Step 4: Send STATUS_OK to indicate success
	unsigned char status = STATUS_OK;
	if (send_all(client_fd, &status, 1) <= 0) {
		closedir(dir);
		return -1;
	}

	// Step 5: Iterate through directory entries
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		// Skip current directory and parent directory
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		// Determine entry type byte from d_type
		unsigned char type_byte;
		if (entry->d_type == DT_DIR)
			type_byte = LIST_TYPE_DIR;
		else
			type_byte = LIST_TYPE_FILE;  /* DT_REG, DT_LNK, DT_UNKNOWN, etc. */

		// Send entry: type byte + length (4-byte big-endian) + name string
		uint32_t len = (uint32_t)strlen(entry->d_name);
		uint32_t len_be = htonl(len);

		if (send_all(client_fd, &type_byte, 1) <= 0 ||
		    send_all(client_fd, &len_be, sizeof(len_be)) <= 0 ||
		    send_all(client_fd, entry->d_name, len) <= 0) {
			perror("send directory entry");
			closedir(dir);
			return -1;
		}
	}

	// Step 6: Send end-of-list marker (LIST_TYPE_EOL byte, no length/name follows)
	unsigned char eol = LIST_TYPE_EOL;
	send_all(client_fd, &eol, 1);

	closedir(dir);
	printf("Directory listing sent.\n");
	return 0;
}

/* ========== Public API ========== */

/**
 * @brief Main entry point for handling an authenticated client session
 *
 * Called by main.c after receiving the unlock byte (0x01).
 * This function orchestrates the entire session:
 * 1. Authenticate user (receive username for tilde expansion)
 * 2. Receive mode byte (D=download, U=upload, L=list)
 * 3. Dispatch to appropriate handler
 *
 * Protocol sequence:
 * - Client sends: 4-byte length + username string
 * - Client sends: 1-byte mode ('D', 'U', or 'L')
 * - Handler takes over based on mode
 *
 * @param client_fd Connected client socket (already unlocked)
 * @return 0 on success, -1 on error or unknown mode
 *
 * @note Sets global current_username for tilde expansion
 * @note Closes connection after handling (or on error)
 * @note Returns to main.c's idle state after completion
 */
int handle_unlocked_session(int client_fd) {
    // Step 1: Receive and store username for path expansion
    char *username = recv_path_alloc(client_fd);
    if (!username) {
        printf("Invalid or missing username.\n");
        return -1;
    }
    // Store username globally for expand_tilde() to use
    strncpy(current_username, username, sizeof(current_username) - 1);
    current_username[sizeof(current_username) - 1] = '\0';  // Ensure null termination
    printf("Authenticated as user: %s\n", current_username);
    free(username);

    // Step 2: Authenticate password hash against system shadow entry
    if (authenticate_user(client_fd) != 0) {
        printf("Authentication failed for user: %s\n", current_username);
        return -1;
    }

    // Step 3: Receive mode byte to determine operation
    unsigned char mode;
    int n = recv_exact(client_fd, &mode, 1);
    if (n <= 0) {
        perror("recv mode");
        return -1;
    }

    // Step 4: Dispatch to appropriate handler
    if (mode == MODE_DOWNLOAD) {
        return handle_download(client_fd);
    } else if (mode == MODE_UPLOAD) {
        return handle_upload(client_fd);
    } else if (mode == MODE_LIST) {
        return handle_list(client_fd);
    }

    printf("Unknown mode byte: 0x%02x\n", mode);
    return -1;
}
