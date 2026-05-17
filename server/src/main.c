/**
 * @file main.c
 * @brief PAP server entry point.
 *
 * Listens for incoming TCP connections and spawns a detached thread for each
 * accepted client.  Up to MAX_CLIENTS simultaneous sessions are supported;
 * additional connections from the same IP are rejected.
 *
 * Each thread:
 *   1. Waits for the unlock byte (0x01).
 *   2. Hands the socket off to handle_unlocked_session() in session.c.
 *   3. Closes the socket and marks its slot free when done.
 *
 * Logging goes to /opt/logs/server.log (server-level events) and
 * /opt/logs/client_<ip>.log (per-client events).  The directory must
 * exist before the server starts (or be created with the right permissions).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <threads.h>

#include "logging.h"
#include "session.h"

/* ── Configuration ──────────────────────────────────────────────────────── */

#define PORT          9001
#define UNLOCK_SIGNAL 0x01
#define MAX_CLIENTS   64

/**
 * Accept timeout in seconds.  accept() will unblock this often so the main
 * loop can perform housekeeping (reaping finished slots, checking flags).
 * Set to 0 to disable the timeout (accept blocks indefinitely).
 */
#define ACCEPT_TIMEOUT_SECONDS 1

/* ── ClientSlot ─────────────────────────────────────────────────────────── */

typedef struct {
    int  socket;
    char ip[16];  /* IPv4 dotted-decimal, e.g. "192.168.1.1\0" */
    char done;    /* Thread sets to 1 when finished; main thread cleans up. */
} ClientSlot;

static ClientSlot g_clients[MAX_CLIENTS];

/**
 * @brief Find a free slot, initialise it, and return a pointer to it.
 * @return Pointer to the allocated slot, or NULL if all slots are in use.
 */
static ClientSlot *allocate_client_slot(int socket, const char *ip)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].socket == 0) {
            g_clients[i].socket = socket;
            strncpy(g_clients[i].ip, ip, sizeof(g_clients[i].ip) - 1);
            g_clients[i].ip[sizeof(g_clients[i].ip) - 1] = '\0';
            g_clients[i].done = 0;
            return &g_clients[i];
        }
    }
    return NULL;
}

/**
 * @brief Return 1 if an active (not-done) slot already exists for this IP.
 *        Only call from the main thread.
 */
static int is_ip_connected(const char *ip)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].socket != 0 && !g_clients[i].done) {
            if (strcmp(g_clients[i].ip, ip) == 0)
                return 1;
        }
    }
    return 0;
}

/**
 * @brief Free every slot whose thread has set done = 1.
 *        Only call from the main thread.
 */
static void reap_finished_clients(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].socket != 0 && g_clients[i].done) {
            g_clients[i] = (ClientSlot){0};
        }
    }
}

/* ── Client thread ──────────────────────────────────────────────────────── */

/**
 * @brief Entry point for each client thread.
 *
 * Receives the unlock byte, then delegates to handle_unlocked_session().
 * Marks the slot done when the session ends (for any reason).
 *
 * @param arg Pointer to the ClientSlot allocated for this connection.
 * @return    0 on success, -1 on error.
 */
static int client_thread(void *arg)
{
    ClientSlot *slot = (ClientSlot *)arg;

    client_log(slot->ip, "[%s] Client connected.", slot->ip);

    /* Wait for the unlock byte. */
    unsigned char sig;
    int n = recv(slot->socket, &sig, 1, MSG_WAITALL);
    if (n <= 0 || sig != UNLOCK_SIGNAL) {
        client_log(slot->ip, "[%s] Bad or missing unlock signal (received 0x%02x).",
                   slot->ip, (n > 0) ? sig : 0);
        close(slot->socket);
        slot->done = 1;
        return -1;
    }

    client_log(slot->ip, "[%s] Unlock signal received, starting session.", slot->ip);

    if (handle_unlocked_session(slot->socket, slot->ip) != 0) {
        client_log(slot->ip, "[%s] Session aborted due to error.", slot->ip);
    }

    close(slot->socket);
    client_log(slot->ip, "[%s] Session ended, slot released.", slot->ip);
    slot->done = 1;
    return 0;
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    /* Ensure the log directory exists. */
    mkdir(LOG_DIR, 0755);

    /* Create the listening socket. */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    /* Allow immediate port reuse after server restart. */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        return 1;
    }

    /* Set a short receive timeout so accept() unblocks periodically for
     * housekeeping (reaping finished slots, checking future shutdown flags). */
#if ACCEPT_TIMEOUT_SECONDS > 0
    struct timeval accept_timeout = { .tv_sec = ACCEPT_TIMEOUT_SECONDS, .tv_usec = 0 };
    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &accept_timeout, sizeof(accept_timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        close(server_fd);
        return 1;
    }
#endif

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(PORT),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return 1;
    }
    if (listen(server_fd, 5) < 0) {
        perror("listen"); close(server_fd); return 1;
    }

    server_log("Server listening on port %d (max %d concurrent clients).",
               PORT, MAX_CLIENTS);

    while (1) {
        /* Sweep slots left behind by finished threads before accepting. */
        reap_finished_clients();

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Timeout — normal, just loop again. */
                continue;
            }
            perror("accept");
            continue;
        }

        /* inet_ntoa() returns a pointer to a static buffer — copy immediately. */
        char client_ip[16];
        strncpy(client_ip, inet_ntoa(client_addr.sin_addr), sizeof(client_ip) - 1);
        client_ip[sizeof(client_ip) - 1] = '\0';

        server_log("Connection attempt from %s:%d",
                   client_ip, ntohs(client_addr.sin_port));

        /* Reject duplicate connections from the same IP. */
        if (is_ip_connected(client_ip)) {
            server_log("Rejected duplicate connection from %s", client_ip);
            close(client_fd);
            continue;
        }

        /* Reject if all slots are full. */
        ClientSlot *slot = allocate_client_slot(client_fd, client_ip);
        if (slot == NULL) {
            server_log("Max clients (%d) reached, rejecting %s", MAX_CLIENTS, client_ip);
            close(client_fd);
            continue;
        }

        /* Clear the accept() timeout inherited by the client socket so that
         * session recv() calls block until data actually arrives. */
        struct timeval no_timeout = { .tv_sec = 0, .tv_usec = 0 };
        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                       &no_timeout, sizeof(no_timeout)) < 0) {
            perror("setsockopt clear timeout");
            close(client_fd);
            slot->done = 1;
            continue;
        }

        server_log("Client accepted: %s:%d",
                   client_ip, ntohs(client_addr.sin_port));

        /* Spawn a detached thread to handle this client. */
        thrd_t tid;
        if (thrd_create(&tid, client_thread, slot) != thrd_success) {
            perror("thrd_create");
            close(client_fd);
            slot->done = 1;
            continue;
        }
        thrd_detach(tid);
    }

    close(server_fd);
    return 0;
}