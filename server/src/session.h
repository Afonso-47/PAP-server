#ifndef SESSION_H
#define SESSION_H

/**
 * @brief Main entry point for handling an authenticated client session.
 *
 * Called by the client thread after the unlock byte (0x01) has been received.
 * Orchestrates authentication, mode selection, and file transfer.
 *
 * @param client_fd Connected client socket (already unlocked).
 * @param client_ip Client IPv4 address string, used for per-client logging.
 * @return 0 on success, -1 on error or unknown mode.
 */
int handle_unlocked_session(int client_fd, const char *client_ip);

#endif /* SESSION_H */