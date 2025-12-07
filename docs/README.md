# PAP Server/Client Workflow

This app uses a simple TCP protocol on port 9001 for secure bidirectional file transfer with user authentication and tilde path expansion.

## Session Flow
1) Client connects to the server on port 9001.
2) Client sends unlock byte `0x01`.
3) Client sends username: 4-byte BE length + UTF-8 username (for tilde expansion on server).
4) Client sends a mode byte: `D` (download server→client) or `U` (upload client→server).
5) Client sends a path (which file to download OR where to save upload).
6) Server sends status byte: 0x00 (OK) or 0x01 (ERROR).
7) If OK, data transfer flows according to the mode; if ERROR, connection closes.

## Downloading a file from the server
- Client specifies which server-side file to download (supports tilde expansion).
- Client sends: mode `D`, then 4-byte BE path length + UTF-8 path of desired file.
- Server sends: 1-byte status (0x00=OK, 0x01=ERROR).
- If OK: server sends 4-byte BE filename length + basename + file data until EOF.
- If ERROR: server closes connection (file not found or permission denied).

### CLI Example
```bash
python client/src/client.py 10.0.0.1 9001 download ~/reports/data.csv ./downloads
```
This downloads `~/reports/data.csv` from the server (expanded to `/home/user/reports/data.csv` on server side) and saves it to `./downloads/data.csv` locally.

### Interactive TUI
```bash
python client/src/main.py
```
Provides an interactive menu for downloads and uploads with prompts for all parameters.

## Uploading a file to the server
- Client specifies local file and target path on server (supports tilde expansion).
- Server creates parent directories automatically if they don't exist.
- Client sends: mode `U`, then 4-byte BE target path length + UTF-8 target path + file data.
- Server sends: 1-byte status (0x00=OK, 0x01=ERROR) before receiving file data.
- If OK: server receives and saves file data; if ERROR: connection closes (mkdir failed, etc.).

### CLI Examples
```bash
# Upload with tilde expansion (saves to user's home directory)
python client/src/client.py 10.0.0.1 9001 upload ./report.txt ~/uploads/report.txt

# Upload with absolute path (creates /tmp/uploads/ if needed)
python client/src/client.py 10.0.0.1 9001 upload ./localfile.txt /tmp/uploads/file.txt

# Upload with basename only (saves to server's current working directory)
python client/src/client.py 10.0.0.1 9001 upload ./localfile.txt
```

## Build & Run (server side)
```bash
cd server
gcc src/main.c src/session.c -o server_app -Wall -Wextra
./server_app
```
Server will output: `Server running on port 9001, idle mode`

## Protocol Summary (Binary Format)
**Download**: 
```
unlock(0x01) → username_len(4B BE) → username(UTF-8) → mode(D) → path_len(4B BE) → remote_path(UTF-8) 
→ status(1B) → [if OK: name_len(4B BE) → basename(UTF-8) → file_data]
```

**Upload**: 
```
unlock(0x01) → username_len(4B BE) → username(UTF-8) → mode(U) → path_len(4B BE) → target_path(UTF-8) 
→ status(1B) → [if OK: file_data]
```

## Client Usage
### CLI Mode
```bash
python client/src/client.py <host> <port> download <remote_path> [output_dir]
python client/src/client.py <host> <port> upload <local_file> [remote_target_path]
```

Defaults:
- Download output dir: current directory (`.`)
- Upload target path: basename of local file
- Username: `root` (prompted if not available)

### Interactive TUI Mode
```bash
python client/src/main.py
```
Menu-driven interface with prompts for server details, paths, and file operations.

## Tilde Expansion
- Server expands `~` to the authenticated user's home directory (via `getpwnam()` lookup).
- `~/file.txt` → `/home/username/file.txt`
- `~otheruser/dir/file.txt` → `/home/otheruser/dir/file.txt` (if user exists)
- Absolute paths work without expansion: `/opt/data/file.txt` is used literally.

## Error Handling
- **Server errors** (file not found, permission denied, mkdir failure) → status byte 0x01 sent, connection closes.
- **Client errors** (connection refused, socket error) → graceful error message displayed.
- **Protocol errors** (invalid lengths, corrupted data) → connection closes with error logging.
