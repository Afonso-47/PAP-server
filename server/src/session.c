#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "session.h"

#define BUFFER_SIZE 4096
#define MODE_DOWNLOAD 'D'
#define MODE_UPLOAD   'U'

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

static const char *pick_server_file_path(void) {
	const char *env_path = getenv("SERVER_SEND_FILE");
	return env_path && env_path[0] ? env_path : "./server_file.bin";
}

static const char *path_basename(const char *path) {
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static int handle_download(int client_fd) {
	const char *path = pick_server_file_path();
	const char *name = path_basename(path);
	uint32_t name_len = (uint32_t)strlen(name);
	char buffer[BUFFER_SIZE];

	FILE *f = fopen(path, "rb");
	if (!f) {
		perror("fopen");
		return -1;
	}

	uint32_t name_len_be = htonl(name_len);
	if (send_all(client_fd, &name_len_be, sizeof(name_len_be)) <= 0) {
		perror("send filename length");
		fclose(f);
		return -1;
	}
	if (send_all(client_fd, name, name_len) <= 0) {
		perror("send filename");
		fclose(f);
		return -1;
	}

	printf("Sending file to client: %s\n", path);

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
	uint32_t name_len_be;
	int n = recv_exact(client_fd, &name_len_be, sizeof(name_len_be));
	if (n <= 0) {
		perror("recv filename length");
		return -1;
	}

	uint32_t name_len = ntohl(name_len_be);
	if (name_len == 0 || name_len > 4096) {
		printf("Invalid filename length.\n");
		return -1;
	}

	char *filename = malloc(name_len + 1);
	if (!filename) {
		perror("malloc");
		return -1;
	}

	n = recv_exact(client_fd, filename, name_len);
	if (n <= 0) {
		perror("recv filename");
		free(filename);
		return -1;
	}
	filename[name_len] = '\0';

	printf("Receiving file for path: %s\n", filename);

	FILE *f = fopen(filename, "wb");
	if (!f) {
		perror("fopen");
		free(filename);
		return -1;
	}

	free(filename);

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

int handle_unlocked_session(int client_fd) {
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
	}

	printf("Unknown mode byte: 0x%02x\n", mode);
	return -1;
}
