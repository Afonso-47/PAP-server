#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "session.h"

#define PORT 9001
#define UNLOCK_SIGNAL 0x01

int main(void) {
	int server_fd, client_fd;
	struct sockaddr_in addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) { perror("socket"); exit(1); }

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(PORT);

	if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("bind"); exit(1);
	}
	if (listen(server_fd, 5) < 0) {
		perror("listen"); exit(1);
	}

	printf("Server running on port %d, idle mode\n", PORT);

	while (1) {
		client_fd = accept(server_fd, NULL, NULL);
		if (client_fd < 0) { perror("accept"); continue; }

		printf("Client connected, waiting for unlock...\n");

		unsigned char sig;
		int n = recv(client_fd, &sig, 1, MSG_WAITALL);
		if (n <= 0 || sig != UNLOCK_SIGNAL) {
			printf("Bad or missing unlock signal.\n");
			close(client_fd);
			continue;
		}

		printf("Unlock signal received, starting transfer.\n");

		if (handle_unlocked_session(client_fd) != 0) {
			printf("Transfer aborted due to error.\n");
		}

		close(client_fd);
		printf("Session done, returning to idle mode.\n");
	}

	close(server_fd);
	return 0;
}