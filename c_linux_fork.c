/*
Simple compilation:
gcc -Wall -Wextra c_linux_fork.c -o my_proxy
Recommended compilation (for routers):
gcc -Wall -Wextra -DDAEMON -DBUFFER_SIZE=512 c_linux_fork.c -o my_proxy

List of possible defines:
DEBUG - show error and info messages (perror, printf and fprintf to stderr)
DAEMON - server will start as background process (for more info check daemonize function below)
BUFFER_SIZE=N - set size of pipe buffers to N bytes (default 4096)
IGNORE_SIGPIPE - enable SIGPIPE ignoring (it was necessary in pthread version)

Compilation with all defines (just as an example):
gcc -Wall -Wextra -DDEBUG -DDAEMON -DBUFFER_SIZE=1024 -DIGNORE_SIGPIPE c_linux_fork.c -o my_proxy
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <sys/stat.h>

#ifndef BUFFER_SIZE
#define BUFFER_SIZE 4096
#endif

#ifdef DAEMON
void daemonize(void) {
    pid_t pid;
    pid = fork();
    if (pid < 0) {
#ifdef DEBUG
        perror("fork");
#endif
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
#ifdef DEBUG
        perror("setsid");
#endif
        exit(EXIT_FAILURE);
    }
    pid = fork();
    if (pid < 0) {
#ifdef DEBUG
        perror("fork");
#endif
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setpgid(0, 0) < 0) {
#ifdef DEBUG
        perror("setpgid");
#endif
        exit(EXIT_FAILURE);
    }
    if (chdir("/") < 0) {
#ifdef DEBUG
        perror("chdir");
#endif
        exit(EXIT_FAILURE);
    }
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
#ifdef DEBUG
        perror("open");
#endif
        exit(EXIT_FAILURE);
    }
    if (dup2(fd, STDIN_FILENO) < 0) exit(EXIT_FAILURE);
    if (dup2(fd, STDOUT_FILENO) < 0) exit(EXIT_FAILURE);
    if (dup2(fd, STDERR_FILENO) < 0) exit(EXIT_FAILURE);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}
#endif

typedef struct {
    int from_fd;
    int to_fd;
} pipe_args_t;

ssize_t read_n(int fd, void *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, (char*)buf + total, n - total);
        if (r <= 0) return r;
        total += r;
    }
    return total;
}

ssize_t write_n(int fd, const void *buf, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, (const char*)buf + total, n - total);
        if (w <= 0) return w;
        total += w;
    }
    return total;
}

void pipe_data(int from_fd, int to_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(from_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t w = write(to_fd, buffer + sent, n - sent);
            if (w <= 0) {
                if (w < 0) {
                    if (errno == EPIPE || errno == ECONNRESET) {
                        goto cleanup;
                    }
#ifdef DEBUG
                    perror("write");
#endif
                }
                goto cleanup;
            }
            sent += w;
        }
    }
#ifdef DEBUG
    if (n < 0) {
        perror("read");
    }
#endif
cleanup:
    shutdown(to_fd, SHUT_WR);
    shutdown(from_fd, SHUT_RD);
}

int fragment_data(int local_fd, int remote_fd) {
    uint8_t head[5];
    ssize_t n = read_n(local_fd, head, 5);
    if (n != 5) return -1;
    uint8_t data[2048];
    n = read(local_fd, data, sizeof(data));
    if (n <= 0) return -1;
    size_t data_len = (size_t)n;
    size_t offset = 0;
    uint8_t part_buf[3 + 2 + 2048];
    uint8_t *zero_ptr = memchr(data, 0x00, data_len);
    if (zero_ptr) {
        size_t host_end_index = zero_ptr - data;
        part_buf[0] = 0x16;
        part_buf[1] = 0x03;
        part_buf[2] = 0x04;
        uint16_t len_be = htons((uint16_t)(host_end_index + 1));
        memcpy(part_buf + 3, &len_be, 2);
        memcpy(part_buf + 5, data, host_end_index + 1);
        if (write_n(remote_fd, part_buf, 5 + host_end_index + 1) < 0) return -1;
        offset = host_end_index + 1;
        data_len -= offset;
    }
    while (data_len > 0) {
        size_t max_len = data_len;
        size_t part_len = (rand() % max_len) + 1;
        part_buf[0] = 0x16;
        part_buf[1] = 0x03;
        part_buf[2] = 0x04;
        uint16_t len_be = htons((uint16_t)part_len);
        memcpy(part_buf + 3, &len_be, 2);
        memcpy(part_buf + 5, data + offset, part_len);
        if (write_n(remote_fd, part_buf, 5 + part_len) < 0) return -1;
        offset += part_len;
        data_len -= part_len;
    }
    return 0;
}

int connect_remote(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res, *rp;
    int sock = -1;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
#ifdef DEBUG
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        fprintf(stderr, "connect_remote: host='%s', port='%s'\n", host, port);
#endif
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

void handle_pipe(int from_fd, int to_fd) {
    pipe_data(from_fd, to_fd);
    close(from_fd);
    close(to_fd);
    _exit(0);
}

void handle_client_process(int client_fd) {
    char buffer[1500];
    ssize_t n = read(client_fd, buffer, sizeof(buffer));
    if (n <= 0) goto cleanup;
    char *line_end = memchr(buffer, '\n', n);
    if (!line_end) goto cleanup;
    size_t line_len = line_end - buffer;
    char line[1024];
    if (line_len >= sizeof(line)) goto cleanup;
    memcpy(line, buffer, line_len);
    line[line_len] = 0;
    char method[16], target[256];
    if (sscanf(line, "%15s %255s", method, target) != 2) goto cleanup;
    if (strcmp(method, "CONNECT") != 0) goto cleanup;
    char *colon = strchr(target, ':');
    if (!colon) goto cleanup;
    *colon = 0;
    const char *host = target;
    const char *port = colon + 1;
    int remote_fd = connect_remote(host, port);
    if (remote_fd < 0) goto cleanup;
    const char *resp = "HTTP/1.1 200 OK\r\n\r\n";
    if (write_n(client_fd, resp, strlen(resp)) < 0) {
        close(remote_fd);
        goto cleanup;
    }
    if (strcmp(port, "443") == 0) {
        if (fragment_data(client_fd, remote_fd) < 0) {
            close(remote_fd);
            goto cleanup;
        }
    }
    pid_t pid1 = fork();
    if (pid1 < 0) {
#ifdef DEBUG
        perror("fork");
#endif
        close(remote_fd);
        goto cleanup;
    }
    if (pid1 == 0) {
        handle_pipe(client_fd, remote_fd);
    }
    pid_t pid2 = fork();
    if (pid2 < 0) {
#ifdef DEBUG
        perror("fork");
#endif
        kill(pid1, SIGTERM);
        close(remote_fd);
        goto cleanup;
    }
    if (pid2 == 0) {
        handle_pipe(remote_fd, client_fd);
    }
    close(client_fd);
    close(remote_fd);
    int status;
    waitpid(pid1, &status, 0);
    waitpid(pid2, &status, 0);
    _exit(0);
cleanup:
    close(client_fd);
    _exit(1);
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
#ifdef DEBUG
        fprintf(stderr, "Usage: %s ip port\n", argv[0]);
#endif
        return -1;
    }
    char* LISTEN_IP = argv[1];
    uint16_t LISTEN_PORT;
    if(sscanf(argv[2], "%hu", &LISTEN_PORT) != 1) {
#ifdef DEBUG
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
#endif
        return -1;
    }
#ifdef DAEMON
    daemonize();
#endif
#ifdef IGNORE_SIGPIPE
    signal(SIGPIPE, SIG_IGN); /* required in pthread version, but (as I know) unnecessary in this fork version */ 
#endif
    signal(SIGCHLD, SIG_IGN); /* avoid zombie processes (in addition to waitpid in handle_client_process) */
    srand(time(NULL));
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
#ifdef DEBUG
        perror("socket");
#endif
        exit(1);
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LISTEN_PORT);
    if (inet_pton(AF_INET, LISTEN_IP, &addr.sin_addr) != 1) {
#ifdef DEBUG
        fprintf(stderr, "Invalid listen IP address: %s\n", LISTEN_IP);
#endif
        exit(1);
    }
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef DEBUG
        perror("bind");
#endif
        close(listen_fd);
        exit(1);
    }
    if (listen(listen_fd, 128) < 0) {
#ifdef DEBUG
        perror("listen");
#endif
        close(listen_fd);
        exit(1);
    }
#ifdef DEBUG
    printf("Proxy listening on %s:%d\n", LISTEN_IP, LISTEN_PORT);
#endif
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
#ifdef DEBUG
            perror("accept");
#endif
            continue;
        }
        pid_t pid = fork();
        if (pid < 0) {
#ifdef DEBUG
            perror("fork");
#endif
            close(client_fd);
            continue;
        }
        if (pid == 0) {
            close(listen_fd);
            handle_client_process(client_fd);
        }
        close(client_fd);
    }
    close(listen_fd);
    return 0;
}
