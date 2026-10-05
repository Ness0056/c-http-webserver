
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

/**
 * Derives a sockaddr_in structure from the provided host and port information.
 * -> copied from the sheet
 */
static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,   // IPv4 only
    };
    struct addrinfo *result_info;

    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode != 0) {
        fprintf(stderr, "Error parsing host/port: %s\n", gai_strerror(returncode));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in result = *((struct sockaddr_in*) result_info->ai_addr);
    freeaddrinfo(result_info);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *host = argv[1];   // e.g. "0.0.0.0"
    const char *port = argv[2];   // e.g. "1234"

    // 1) Parse host+port into sockaddr_in
    struct sockaddr_in addr = derive_sockaddr(host, port);

    // 2) Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 3) Set SO_REUSEADDR so tests can restart server on same port quickly
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 4) Bind
    if (bind(server_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 5) Listen
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // At this point, Task 2.1 is basically done: socket is listening.
    // Just keep the server running so tests can connect.
    for (;;) {
        pause();   // Do nothing, just stay alive
    }

    // unreachable, but whatever
    close(server_fd);
    return EXIT_SUCCESS;
}
