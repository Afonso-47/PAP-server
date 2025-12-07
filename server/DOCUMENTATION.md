# PAP Server Documentation

## Overview
The server listens on TCP port 9001 and supports bidirectional file transfer with user authentication and tilde path expansion. After an unlock signal, clients authenticate with a username, specify whether to download or upload a file, and provide the file path to use.

## Files
- `src/main.c`: sets up the listening socket, waits for the unlock byte (0x01), then delegates the connection to `handle_unlocked_session` and returns to idle after completion.
- `src/session.c`: implements `handle_unlocked_session`, handling user authentication (username), both download (server→client) and upload (client→server) modes, tilde path expansion using `pwd.h`, and status byte error reporting.
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
4) Client sends a 1-byte mode: `D` (download) or `U` (upload).
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
- `expand_tilde(const char *path)`: Expands `~` and `~user/` prefixes; returns malloc'd string or original path copy.
- `ensure_parent_dirs(const char *path)`: Recursively creates parent directories with mode 0755.
- `recv_path_alloc(int fd)`: Receives 4-byte BE length + UTF-8 path; returns malloc'd null-terminated string.
- `send_all()`, `recv_exact()`: Reliable send/receive wrappers for partial socket operations.
- `path_basename(const char *path)`: Returns pointer to filename portion (after last `/`).
