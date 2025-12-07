# Changelog

## 2025-12-07
- Split server logic: `main.c` now only listens and waits for unlock before delegating.
- Added `session.c`/`session.h` to handle unlocked sessions and stream a server-side file to clients.
- Added bidirectional transfer: after unlock, the server reads a mode byte (`D` download server→client, `U` upload client→server).
- Download: server picks file from `SERVER_SEND_FILE` or `./server_file.bin` and sends filename length/name then file data.
- Upload: server receives filename length/name then file data and saves it locally.
