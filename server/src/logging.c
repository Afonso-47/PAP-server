/**
 * @file logging.c
 * @brief Timestamped logging implementation for PAP server.
 *
 * All messages are written to stdout and to a log file under /opt/logs/.
 * The directory is assumed to exist (created once at server startup via mkdir).
 */

#include "logging.h"

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

/* ========== Internal helpers ========== */

/**
 * @brief Build a "[dd/mm/yyyy - hh:mm:ss] " timestamp string.
 * @param buf      Output buffer.
 * @param buf_size Capacity of buf (at least 28 bytes recommended).
 */
static void build_timestamp(char *buf, size_t buf_size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, buf_size, "[%d/%m/%Y - %H:%M:%S] ", t);
}

/* ========== Public API ========== */

void server_log(const char *format, ...)
{
    char timestamp[28];
    build_timestamp(timestamp, sizeof(timestamp));

    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    printf("%s%s\n", timestamp, message);

    FILE *log_file = fopen(SERVER_LOG_FILE, "a");
    if (log_file != NULL) {
        fprintf(log_file, "%s%s\n", timestamp, message);
        fclose(log_file);
    }
}

void client_log(const char *client_ip, const char *format, ...)
{
    char timestamp[28];
    build_timestamp(timestamp, sizeof(timestamp));

    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    printf("%s%s\n", timestamp, message);

    /* Write to per-client log file. */
    char client_log_path[256];
    snprintf(client_log_path, sizeof(client_log_path),
             "%sclient_%s.log", LOG_DIR, client_ip);

    FILE *client_file = fopen(client_log_path, "a");
    if (client_file != NULL) {
        fprintf(client_file, "%s%s\n", timestamp, message);
        fclose(client_file);
    }
}