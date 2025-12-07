# Changelog

## 2025-12-07
- Client now supports both download (server→client) and upload (client→server) after sending unlock byte (0x01).
- Mode byte added: `D` for download, `U` for upload; helpers to send mode, receive file, and upload file with filename metadata.
- CLI updated: `python client.py <host> <port> <download|upload> [...]` with optional output dir for download and target name for upload.
