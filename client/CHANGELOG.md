# Changelog

## 2025-12-07 (Features & Stability)
- Refactored client into modular API functions and created TUI (`main.py`) interface for easier interaction.
- Added username authentication: sent to server after unlock for tilde (`~`) expansion.
- Implemented status byte handling (0x00=OK, 0x01=ERROR) to gracefully handle server errors instead of crashing on UTF-8 decode errors.
- Enhanced error messages to inform users when files don't exist or paths are inaccessible.
- Both CLI and TUI now prompt for username with default (root) for path expansion.

## 2025-12-07
- Client now supports both download (server→client) and upload (client→server) after sending unlock byte (0x01).
- Mode byte added: `D` for download, `U` for upload; added `send_path` helper to specify remote file paths.
- Download: client specifies which server file to download via `<remote_path>` argument, with optional `[output_dir]`.
- Upload: client specifies local file and optional target path on server (with directory support); defaults to basename if not specified.
- CLI: `python client.py <host> <port> download <remote_path> [output_dir]` or `python client.py <host> <port> upload <local_file> [remote_target_path]`.
