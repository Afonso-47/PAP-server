#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <wordexp.h>

#include "session.h"

#define BUFFER_SIZE 4096
#define MODE_DOWNLOAD 'D'
#define MODE_UPLOAD   'U'
#define MODE_LIST     'L'
#define STATUS_OK 0x00
#define STATUS_ERROR 0x01

static char current_username[256] = {0};

static int recv_exact(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        int n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return (int)total;
}

static char *recv_path_alloc(int fd) {
    uint32_t len_be;
    int n = recv_exact(fd, &len_be, sizeof(len_be));
    if (n <= 0) return NULL;

    uint32_t len = ntohl(len_be);
    if (len == 0 || len > 4096) return NULL;

    char *path = malloc(len + 1);
    if (!path) return NULL;

    n = recv_exact(fd, path, len);
    if (n <= 0) { free(path); return NULL; }
    path[len] = '\0';
    return path;
}

static int send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (const char *)buf;
    while (total < len) {
        int n = send(fd, p + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return (int)total;
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static char *expand_tilde(const char *path) {
    if (!path || path[0] != '~') {
        return strdup(path);
    }

    const char *home = NULL;
    const char *rest = path + 1;

    if (rest[0] == '/' || rest[0] == '\0') {
        if (current_username[0] != '\0') {
            struct passwd *pw = getpwnam(current_username);
            if (pw) {
                home = pw->pw_dir;
            }
        }
        if (!home) {
            home = getenv("HOME");
        }
        if (!home) {
            home = "/root";
        }
    } else {
        const char *slash = strchr(rest, '/');
        size_t userlen = slash ? (size_t)(slash - rest) : strlen(rest);
        char username[256];
        if (userlen < sizeof(username)) {
            memcpy(username, rest, userlen);
            username[userlen] = '\0';
            struct passwd *pw = getpwnam(username);
            if (pw) {
                home = pw->pw_dir;
                rest = slash ? slash : "";
            }
        }
    }

    if (!home) {
        return strdup(path);
    }

    size_t homelen = strlen(home);
    size_t restlen = strlen(rest);
    char *expanded = malloc(homelen + restlen + 1);
    if (!expanded) return NULL;

    memcpy(expanded, home, homelen);
    memcpy(expanded + homelen, rest, restlen + 1);
    return expanded;
}

static int ensure_parent_dirs(const char *path) {
    char tmp[4096 + 1];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                return -1;
            }
            tmp[i] = '/';
        }
    }
    return 0;
}

static int handle_download(int client_fd) {
    char buffer[BUFFER_SIZE];
    char *requested_path = recv_path_alloc(client_fd);
    if (!requested_path) {
        printf("Invalid or missing requested path.\n");
        return -1;
    }

    char *expanded_path = expand_tilde(requested_path);
    free(requested_path);
    if (!expanded_path) {
        printf("Path expansion failed.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    const char *name = path_basename(expanded_path);
    uint32_t name_len = (uint32_t)strlen(name);
    
    // Make a copy of the name before freeing expanded_path
    char name_copy[4096];
    if (name_len >= sizeof(name_copy)) {
        printf("Filename too long.\n");
        free(expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }
    memcpy(name_copy, name, name_len + 1);

    FILE *f = fopen(expanded_path, "rb");
    if (!f) {
        perror("fopen");
        printf("Hint: ensure requested file exists: %s\n", expanded_path);
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    printf("Sending file to client: %s\n", expanded_path);
    free(expanded_path);

    unsigned char status = STATUS_OK;
    if (send_all(client_fd, &status, 1) <= 0) {
        perror("send status");
        fclose(f);
        return -1;
    }

    uint32_t name_len_be = htonl(name_len);
    if (send_all(client_fd, &name_len_be, sizeof(name_len_be)) <= 0) {
        perror("send filename length");
        fclose(f);
        return -1;
    }
    if (send_all(client_fd, name_copy, name_len) <= 0) {
        perror("send filename");
        fclose(f);
        return -1;
    }

    size_t nread;
    while ((nread = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        if (send_all(client_fd, buffer, nread) <= 0) {
            perror("send file data");
            fclose(f);
            return -1;
        }
    }

    if (ferror(f)) {
        perror("fread");
        fclose(f);
        return -1;
    }

    fclose(f);
    printf("File sent.\n");
    return 0;
}

static int handle_upload(int client_fd) {
    char buffer[BUFFER_SIZE];
    char *target_path = recv_path_alloc(client_fd);
    if (!target_path) {
        printf("Invalid or missing target path.\n");
        return -1;
    }

    char *expanded_path = expand_tilde(target_path);
    free(target_path);
    if (!expanded_path) {
        printf("Path expansion failed.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    if (ensure_parent_dirs(expanded_path) != 0) {
        perror("mkdir");
        free(expanded_path);
        return -1;
    }

    printf("Receiving file for path: %s\n", expanded_path);

    FILE *f = fopen(expanded_path, "wb");
    if (!f) {
        perror("fopen");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    unsigned char status = STATUS_OK;
    send_all(client_fd, &status, 1);
    free(expanded_path);

    int n;
    while ((n = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        if (fwrite(buffer, 1, n, f) != (size_t)n) {
            perror("fwrite");
            fclose(f);
            return -1;
        }
    }

    if (n < 0) {
        perror("recv file data");
    }

    fclose(f);
    if (n < 0) {
        return -1;
    }

    printf("File saved.\n");
    return 0;
}

static int handle_list(int client_fd) {
    char *dir_path = recv_path_alloc(client_fd);
    if (!dir_path) {
        printf("Invalid or missing directory path.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    char *expanded_path = expand_tilde(dir_path);
    free(dir_path);
    if (!expanded_path) {
        printf("Path expansion failed.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    // Build the ls command with the expanded path
    char cmd[4096];
    int written = snprintf(cmd, sizeof(cmd), "ls -la '%s'", expanded_path);
    if (written < 0 || written >= (int)sizeof(cmd)) {
        printf("Command too long.\n");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        free(expanded_path);
        return -1;
    }

    printf("Listing directory: %s\n", expanded_path);
    free(expanded_path);

    // Execute ls and capture output
    FILE *ls_pipe = popen(cmd, "r");
    if (!ls_pipe) {
        perror("popen");
        unsigned char status = STATUS_ERROR;
        send_all(client_fd, &status, 1);
        return -1;
    }

    unsigned char status = STATUS_OK;
    if (send_all(client_fd, &status, 1) <= 0) {
        perror("send status");
        pclose(ls_pipe);
        return -1;
    }

    // Read ls output and send to client
    char buffer[BUFFER_SIZE];
    size_t nread;
    while ((nread = fread(buffer, 1, BUFFER_SIZE, ls_pipe)) > 0) {
        if (send_all(client_fd, buffer, nread) <= 0) {
            perror("send ls output");
            pclose(ls_pipe);
            return -1;
        }
    }

    pclose(ls_pipe);
    printf("Directory listing sent.\n");
    return 0;
}

int handle_unlocked_session(int client_fd) {
    char *username = recv_path_alloc(client_fd);
    if (!username) {
        printf("Invalid or missing username.\n");
        return -1;
    }
    strncpy(current_username, username, sizeof(current_username) - 1);
    current_username[sizeof(current_username) - 1] = '\0';
    printf("Authenticated as user: %s\n", current_username);
    free(username);

    unsigned char mode;
    int n = recv_exact(client_fd, &mode, 1);
    if (n <= 0) {
        perror("recv mode");
        return -1;
    }

    if (mode == MODE_DOWNLOAD) {
        return handle_download(client_fd);
    } else if (mode == MODE_UPLOAD) {
        return handle_upload(client_fd);
    } else if (mode == MODE_LIST) {
        return handle_list(client_fd);
    }

    printf("Unknown mode byte: 0x%02x\n", mode);
    return -1;
}
