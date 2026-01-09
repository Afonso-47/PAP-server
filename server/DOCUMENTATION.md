# PAP Server Documentation

## Overview
The server listens on TCP port 9001 and supports bidirectional file transfer with user authentication and tilde path expansion. After an unlock signal, clients authenticate with a username, specify whether to download or upload a file or list a directory, and provide the file path to use.

## Files
- `src/main.c`: sets up the listening socket, waits for the unlock byte (0x01), then delegates the connection to `handle_unlocked_session` and returns to idle after completion.
- `src/session.c`: implements `handle_unlocked_session`, handling user authentication (username), download (server→client), upload (client→server), and list (directory listing) modes, tilde path expansion using `pwd.h`, and status byte error reporting. **Fully documented with comprehensive comments explaining protocol flow, function behavior, and edge cases.**
- `src/session.h`: declaration for the session handler.

## Build
```bash
# From server/ directory
gcc src/main.c src/session.c -o server_app -Wall -Wextra
```

## Run
```bash
# Start the server (listens on 0.0.0.0:9001)
./server_app
```

## Protocol
1) Client connects to port 9001.
2) Client sends a 1-byte unlock value `0x01`.
3) Client sends username: 4-byte BE length + UTF-8 username string (for tilde expansion).
4) Client sends a 1-byte mode: `D` (download), `U` (upload), or `L` (list).
5a) **Download mode**: 
   - Client sends 4-byte BE path length + UTF-8 path of file to download.
   - Server sends 1-byte status (0x00=OK, 0x01=ERROR).
   - If OK: 4-byte BE filename length + basename + file data until EOF.
   - If ERROR: connection closes (file not found, read error, etc.).
5b) **Upload mode**:
   - Client sends 4-byte BE path length + UTF-8 target path.
   - Server sends 1-byte status (0x00=OK, 0x01=ERROR) indicating if path is writable.
   - If OK: server receives file data until EOF; creates parent directories automatically.
   - If ERROR: connection closes (permission denied, mkdir failed, etc.).
5c) **List mode**:
   - Client sends 4-byte BE path length + UTF-8 directory path.
   - Server sends 1-byte status (0x00=OK, 0x01=ERROR).
   - If OK: server sends directory entries, each as 4-byte BE length + entry name (excludes "." and "..").
   - End-of-list signaled by zero-length entry (0x00000000).
   - If ERROR: connection closes (directory not found, permission denied, etc.).
6) Server closes the connection when done and returns to idle.

## Path Expansion
- Tilde (`~`) is expanded to the authenticated user's home directory using `getpwnam()`.
- `~user/path` expands to user's home + `/path` (requires user to exist in system).
- Paths without tilde are used literally (absolute or relative to server's working directory).

## Error Handling (server side)
- Invalid or missing unlock byte: connection is closed and the server returns to idle.
- Invalid username length: STATUS_ERROR (0x01) sent, connection closes.
- Invalid mode byte: connection is closed with error message.
- Download: file not found or permission denied → STATUS_ERROR (0x01) sent, connection closes.
- Upload: directory creation failure or write errors → STATUS_ERROR (0x01) sent, connection closes.

## Internal Functions (session.c)

**All functions below are fully documented in the source code with detailed comments.**

- `recv_exact(int fd, void *buf, size_t len)`: Reliably receives exactly `len` bytes from socket, handling partial reads.
- `recv_path_alloc(int fd)`: Receives 4-byte BE length + UTF-8 string; returns malloc'd null-terminated string. Validates length ≤ 4096.
- `send_all(int fd, const void *buf, size_t len)`: Reliably sends exactly `len` bytes to socket, handling partial writes.
- `path_basename(const char *path)`: Returns pointer to filename portion after last `/` (not a copy).
- `expand_tilde(const char *path)`: Expands `~` and `~user/` prefixes using system password database; returns malloc'd string or original path copy.
- `ensure_parent_dirs(const char *path)`: Recursively creates parent directories with mode 0755; ignores EEXIST errors.
- `handle_download(int client_fd)`: Implements download protocol (server → client file transfer).
- `handle_upload(int client_fd)`: Implements upload protocol (client → server file transfer with auto-mkdir).
- `handle_list(int client_fd)`: Implements directory listing protocol; sends entries excluding "." and "..".
- `handle_unlocked_session(int client_fd)`: Main entry point called by main.c; authenticates user and dispatches to appropriate mode handler.
