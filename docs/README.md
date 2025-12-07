# PAP Server/Client Workflow

This app uses a simple TCP protocol on port 9001. Every session starts with:
1) Client connects to the server.
2) Client sends unlock byte `0x01`.
3) Client sends a mode byte: `D` (download server→client) or `U` (upload client→server).
4) Data transfer flows according to the mode.

## Downloading a file from the server
- Server chooses the file from `SERVER_SEND_FILE` env var or defaults to `./server_file.bin`.
- Server sends: 4-byte big-endian filename length, filename bytes (basename), then file data until EOF.
- Client command example:
```bash
python client/src/client.py 10.0.0.1 9001 download ./downloads
```
This stores the received file under `./downloads/` (created if missing).

## Uploading a file to the server
- Client sends: 4-byte big-endian target path length, target path/name bytes, then file data.
- Server writes the file to the provided path on the server filesystem.
- Client command examples:
```bash
# Upload with same name
python client/src/client.py 10.0.0.1 9001 upload ./localfile.txt

# Upload with custom target name/path on server
python client/src/client.py 10.0.0.1 9001 upload ./localfile.txt /tmp/remote_name.txt
```

## Build & Run (server side)
```bash
cd server
gcc src/main.c src/session.c -o server_app
# optional: export SERVER_SEND_FILE=/path/to/file
./server_app
```

## Client defaults
- Host: `10.0.0.1` (override via CLI args).
- Port: `9001` (override via CLI args).
- Download default output dir: current directory.
- Upload default target name: basename of the local file.
