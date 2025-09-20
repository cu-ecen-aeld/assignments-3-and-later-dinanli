// server/aesdsocket.c

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include <getopt.h>

#define SERVER_PORT 9000
#define BACKLOG 10
#define DATAFILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t g_exit = 0;
static int g_listen_fd = -1;
static int g_client_fd = -1;

static void handle_signal(int signo)
{
    (void)signo;
    g_exit = 1;
    // Log inside handler is async-signal-unsafe in general,
    // but syslog is commonly acceptable on Linux; alternatively set a flag.
    syslog(LOG_INFO, "Caught signal, exiting");
}

static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "First fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // Parent exits
        exit(EXIT_SUCCESS);
    }

    // Child becomes session leader
    if (setsid() == -1) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Second fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // First child exits
        exit(EXIT_SUCCESS);
    }

    // Change working directory to root
    if (chdir("/") == -1) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Redirect stdin, stdout, stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
}

static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    if (sigaction(SIGINT, &sa, NULL) == -1) return -1;
    if (sigaction(SIGTERM, &sa, NULL) == -1) return -1;
    return 0;
}

static int send_file_to_client(int client_fd, const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        return -1;
    }

    char buf[4096];
    ssize_t r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t sent = 0;
        while (sent < r) {
            ssize_t n = send(client_fd, buf + sent, (size_t)(r - sent), 0);
            if (n == -1) {
                if (errno == EINTR) continue;
                close(fd);
                return -1;
            }
            sent += n;
        }
    }
    int saved = errno;
    close(fd);
    if (r == -1) {
        errno = saved;
        return -1;
    }
    return 0;
}

static int append_packet_to_file(const char *path, const char *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd == -1) return -1;

    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n == -1) {
            if (errno == EINTR) continue;
            int saved = errno;
            // flock(fd, LOCK_UN);
            close(fd);
            errno = saved;
            return -1;
        }
        written += (size_t)n;
    }

    // flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

static int server_listen(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return -1;

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    if (listen(fd, BACKLOG) == -1) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return fd;
}

int main(int argc, char *argv[])
{
    openlog("aesdsocket", LOG_PID, LOG_USER);

    bool daemon_mode = false;
    int opt;
    while ((opt = getopt(argc, argv, "d")) != -1) {
        if (opt == 'd') {
            daemon_mode = true;
        } else {
            syslog(LOG_ERR, "Usage: %s [-d]", argv[0]);
            closelog();
            return EXIT_FAILURE;
        }
    }

    if (setup_signals() == -1) {
        syslog(LOG_ERR, "Failed to set signals: %s", strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    g_listen_fd = server_listen(SERVER_PORT);
    if (g_listen_fd == -1) {
        syslog(LOG_ERR, "Socket setup failed: %s", strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }

    // Only daemonize after successfully binding
    if (daemon_mode) {
        daemonize();
    }

    // Accept connections until SIGINT/SIGTERM
    while (!g_exit) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        g_client_fd = accept(g_listen_fd, (struct sockaddr *)&caddr, &clen);
        if (g_client_fd == -1) {
            if (errno == EINTR && g_exit) break;
            if (errno == EINTR) continue;
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        char ipstr[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &caddr.sin_addr, ipstr, sizeof(ipstr));
        syslog(LOG_INFO, "Accepted connection from %s", ipstr);

        // Per-connection processing: receive until client closes
        char recvbuf[1024];
        char *accum = NULL;       // dynamic accumulator for partial packet
        size_t accum_len = 0;     // used bytes
        size_t accum_cap = 0;     // allocated bytes

        bool client_open = true;
        while (client_open && !g_exit) {
            ssize_t n = recv(g_client_fd, recvbuf, sizeof(recvbuf), 0);
            if (n == 0) {
                // client closed
                break;
            } else if (n == -1) {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "recv failed: %s", strerror(errno));
                break;
            }

            // Append recv data to accumulator
            if (accum_len + (size_t)n + 1 > accum_cap) {
                size_t new_cap = (accum_cap == 0) ? 2048 : accum_cap;
                while (accum_len + (size_t)n + 1 > new_cap) new_cap *= 2;
                char *tmp = realloc(accum, new_cap);
                if (!tmp) {
                    syslog(LOG_ERR, "malloc/realloc failed");
                    // Drop the over-length data as permitted
                    // Keep serving connection, but clear accumulator
                    free(accum);
                    accum = NULL;
                    accum_len = 0;
                    accum_cap = 0;
                    continue;
                }
                accum = tmp;
                accum_cap = new_cap;
            }
            memcpy(accum + accum_len, recvbuf, (size_t)n);
            accum_len += (size_t)n;
            accum[accum_len] = '\0';

            // Process complete packets terminated by '\n'
            size_t start = 0;
            for (;;) {
                char *nl = memchr(accum + start, '\n', accum_len - start);
                if (!nl) break;
                size_t pkt_end_idx = (size_t)(nl - accum);
                size_t pkt_len = pkt_end_idx - start + 1; // include '\n'

                // Append this packet to the file
                if (append_packet_to_file(DATAFILE, accum + start, pkt_len) == -1) {
                    syslog(LOG_ERR, "file append failed: %s", strerror(errno));
                    // Continue; next packets may still work
                }

                // After packet append, send full file content to client
                if (send_file_to_client(g_client_fd, DATAFILE) == -1) {
                    syslog(LOG_ERR, "send file failed: %s", strerror(errno));
                    // If send failed hard, likely client is gone
                    client_open = false;
                    break;
                }

                start += pkt_len;
            }

            // Compact remaining partial packet to front
            if (start > 0 && start < accum_len) {
                memmove(accum, accum + start, accum_len - start);
                accum_len -= start;
                accum[accum_len] = '\0';
            } else if (start == accum_len) {
                // consumed all
                accum_len = 0;
                if (accum) accum[0] = '\0';
            }
        }

        free(accum);
        close(g_client_fd);
        g_client_fd = -1;
        syslog(LOG_INFO, "Closed connection from %s", ipstr);
    }

    if (g_client_fd != -1) close(g_client_fd);
    if (g_listen_fd != -1) close(g_listen_fd);

    if (unlink(DATAFILE) == -1 && errno != ENOENT) {
        syslog(LOG_ERR, "Failed to remove %s: %s", DATAFILE, strerror(errno));
    }

    closelog();
    return 0;
}

