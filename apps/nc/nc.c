#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#define BUFFER_SIZE 4096

static volatile int keep_running = 1;

void signal_handler(int signum) {
    if (signum == SIGINT) {
        keep_running = 0;
    }
}

void usage(const char *program) {
    fprintf(stderr, "Usage: %s <host> <port>\n", program);
    exit(1);
}

int create_connection(const char *host, const char *port) {
    struct addrinfo hints, *result, *rp;
    int sockfd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int s = getaddrinfo(host, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    // Try each address until we successfully connect
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }

        close(sockfd);
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "Could not connect to host\n");
        return -1;
    }

    return sockfd;
}

int main(int argc, char *argv[]) {
    char *host = NULL;
    char *port = NULL;

    if (argc != 3) {
        usage(argv[0]);
    }

    host = argv[1];
    port = argv[2];

    // Set signal handling
    signal(SIGINT, signal_handler);

    // Create connection
    int sockfd = create_connection(host, port);
    if (sockfd < 0) {
        return 1;
    }

    // Set standard input to non-blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    char buffer[BUFFER_SIZE];
    fd_set readfds;

    printf("Connected to %s:%s (Press Ctrl+C to quit)\n", host, port);

    while (keep_running) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);

        struct timeval tv = {1, 0}; // 1-second timeout
        int ready = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Check if the socket has data to read
        if (FD_ISSET(sockfd, &readfds)) {
            ssize_t bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0) {
                if (bytes < 0) perror("recv");
                break;
            }
            buffer[bytes] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        // Check if standard input has data to read
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            ssize_t bytes = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (bytes > 0) {
                if (send(sockfd, buffer, bytes, 0) < 0) {
                    perror("send");
                    break;
                }
            }
        }
    }

    close(sockfd);
    return 0;
}
