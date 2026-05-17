/**
 * @file logging.h
 * @brief Timestamped logging to stdout and /opt/logs/
 *
 * Two entry points:
 *   server_log()  – server-wide events → /opt/logs/server.log
 *   client_log()  – per-client events  → /opt/logs/client_<ip>.log
 *                   (also echoed to stdout)
 */

#ifndef LOGGING_H
#define LOGGING_H

#define LOG_DIR          "/opt/logs/"
#define SERVER_LOG_FILE  LOG_DIR "server.log"

/**
 * @brief Log a server-level event (startup, shutdown, connections accepted/rejected).
 * @param format printf-style format string.
 */
void server_log(const char *format, ...);

/**
 * @brief Log a client-level event.  Writes to stdout and /opt/logs/client_<ip>.log.
 * @param client_ip The client's IPv4 address string (used to name the log file).
 * @param format    printf-style format string.
 */
void client_log(const char *client_ip, const char *format, ...);

#endif /* LOGGING_H */