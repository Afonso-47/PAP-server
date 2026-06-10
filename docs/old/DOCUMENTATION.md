# PAP Server and Client Documentation

## Overview
The PAP (Protocol for Authenticated Paths) system provides bidirectional file transfer with user authentication and tilde path expansion. The server listens on TCP port 9001 and supports download, upload, and directory listing operations.

## Server

### Files
- `server/src/main.c`: Sets up the listening socket, waits for the unlock byte (0x01), then delegates the connection to `handle_unlocked_session` and returns to idle after completion.
- `server/src/session.c`: Implements `handle_unlocked_session`, handling user authentication (username), download (server→client), upload (client→server), and list (directory listing) modes, tilde path expansion using `pwd.h`, and status byte error reporting.
- `server/src/session.h`: Declaration for the session handler.

### Build
```bash
# From server/ directory
gcc src/main.c src/session.c -o server_app -Wall -Wextra
```

### Run
```bash
# Start the server (listens on 0.0.0.0:9001)
./server_app
```
2
### Protocol
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

### Path Expansion
- Tilde (`~`) is expanded to the authenticated user's home directory using `getpwnam()`.
- `~user/path` expands to user's home + `/path` (requires user to exist in system).
- Paths without tilde are used literally (absolute or relative to server's working directory).

### Error Handling (server side)
- Invalid or missing unlock byte: connection is closed and the server returns to idle.
- Invalid username length: STATUS_ERROR (0x01) sent, connection closes.
- Invalid mode byte: connection is closed with error message.
- Download: file not found or permission denied → STATUS_ERROR (0x01) sent, connection closes.
- Upload: directory creation failure or write errors → STATUS_ERROR (0x01) sent, connection closes.

## Client

### Files
- `client/src/main2.c`: Implements the client UI using ncurses for interactive file browsing and transfer.
- `client/src/send-file-socket.c`: Provides socket utilities for downloading, uploading, and listing directories on the remote server.

### Build
```bash
# From client/ directory
gcc src/main2.c src/send-file-socket.c -o client_app -lncurses -Wall -Wextra
```

### Run
```bash
# Start the client
./client_app
```

### Features
- Interactive file explorer with directory listing.
- Support for download, upload, and directory listing modes.
- Tilde path expansion for both local and remote paths.
- Authentication with username and password.

## Protocol Flow
1. Client connects to server on port 9001.
2. Client sends unlock byte `0x01`.
3. Client sends username for tilde expansion.
4. Client sends mode byte (`D`, `U`, or `L`).
5. Depending on mode:
   - **Download**: Client specifies file to download, server sends file data.
   - **Upload**: Client specifies target path, server acknowledges, client sends file data.
   - **List**: Client specifies directory, server sends directory entries.
6. Connection closes after transfer or on error.

## Security
- Authentication is performed using system shadow passwords (Linux/macOS only).
- Tilde expansion is restricted to the authenticated user's home directory.
- Non-root users are restricted to their home directory for all operations.

## Changelog
Refer to `server/CHANGELOG.md` for detailed changes and updates.

