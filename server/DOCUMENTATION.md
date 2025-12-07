# PAP Server Documentation

## Overview
The server listens on TCP port 9001, waits for a single-byte unlock signal, and then streams a server-side file to the connected client. The file path to send defaults to `./server_file.bin` and can be overridden with the `SERVER_SEND_FILE` environment variable.

## Files
- `src/main.c`: sets up the listening socket, waits for the unlock byte (0x01), then delegates the connection to `handle_unlocked_session` and returns to idle after completion.
- `src/session.c`: implements `handle_unlocked_session`, sending the filename and file contents to the client once the unlock is accepted.
- `src/session.h`: declaration for the session handler.

## Build
```bash
# From server/ directory
gcc src/main.c src/session.c -o server_app
```

## Run
```bash
# Optional: choose which file to send
export SERVER_SEND_FILE="/path/to/file/to/send"

# Start the server (listens on 0.0.0.0:9001)
./server_app
```

## Protocol
1) Client connects to port 9001.
2) Client sends a 1-byte unlock value `0x01`.
3) Server responds with:
   - 4-byte big-endian unsigned length of the filename.
   - UTF-8 filename bytes (basename of the chosen file).
   - File data bytes until EOF.
4) Server closes the connection when done and returns to idle.

## Configuration
- `SERVER_SEND_FILE`: absolute or relative path to the file the server will send. If unset, the server uses `./server_file.bin`.

## Error Handling (server side)
- Invalid or missing unlock byte: connection is closed and the server returns to idle.
- File open/read or send errors: the server logs the error to stderr and closes the connection.
