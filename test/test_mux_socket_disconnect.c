#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mux_socket.h"

static volatile sig_atomic_t g_sigpipe_count;

static void handle_sigpipe(int signal_number) {
    (void)signal_number;
    g_sigpipe_count++;
}

int main(void) {
    int sockets[2] = {-1, -1};
    struct sigaction action;
    struct sigaction old_action;
    const char payload[] = "rtsp";

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigpipe;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, &old_action) != 0) {
        perror("sigaction");
        return 1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        perror("socketpair");
        return 1;
    }

    close(sockets[1]);
    sockets[1] = -1;
    errno = 0;
    ssize_t ret =
        mux_socket_send_no_signal(sockets[0], payload, sizeof(payload), 0);
    int send_errno = errno;

    close(sockets[0]);
    sigaction(SIGPIPE, &old_action, NULL);

    if (ret != -1 || send_errno != EPIPE) {
        fprintf(stderr, "expected -1/EPIPE, got %zd/%d\n", ret, send_errno);
        return 1;
    }
    if (g_sigpipe_count != 0) {
        fprintf(stderr, "SIGPIPE was delivered %d time(s)\n", (int)g_sigpipe_count);
        return 1;
    }

    printf("[PASS] disconnected RTSP socket returns EPIPE without SIGPIPE\n");
    return 0;
}
