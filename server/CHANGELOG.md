# Changelog

## 2026-01-09
- **TODO**: Refactor the code and make it YOURS. You need to know how it works and to be able to have the confidence to work on it yourself without Vibe Coding.
- **NO MORE AI**:
	- added client sided tilde and root/ expansion
	- Windows users get the Downloads folder and that's it.

## 2026-01-09 (Documentation)
- **CODE DOCUMENTATION**: Added comprehensive comments throughout `session.c`:
  - File header with module overview and feature list
  - Detailed function documentation with parameters, return values, and usage examples
  - Inline comments explaining protocol flow, critical logic, and edge cases
  - Section dividers organizing code into logical groups (socket helpers, path utilities, protocol handlers)
  - Special attention to the use-after-free fix with explanatory comments about pointer lifetime

## 2025-12-07 (Bugfix)
- **CRITICAL FIX**: Fixed use-after-free bug in `session.c` `handle_download()`. 
  - Issue: `name` pointer was freed with `expanded_path` before being sent to client, resulting in garbage bytes corrupting the filename transmission.
  - Solution: Create a copy of the filename in `name_copy` stack buffer before freeing `expanded_path`, use `name_copy` for all send operations.

## 2025-12-07
- Split server logic: `main.c` now only listens and waits for unlock before delegating.
- Added `session.c`/`session.h` to handle unlocked sessions for bidirectional file transfer.
- After unlock, server reads a mode byte (`D` download server→client, `U` upload client→server).
- Download: client specifies which file to request; server sends filename length/name then file data.
- Upload: client specifies target path (with auto-mkdir for parent dirs); server receives filename length/name then file data and saves it.
- Added username authentication for tilde (`~`) path expansion using `pwd.h` database lookup.
- Implemented status byte protocol (0x00=OK, 0x01=ERROR) for graceful error handling.
