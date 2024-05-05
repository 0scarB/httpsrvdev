#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "lib.h"

struct httpsrvdev_inst httpsrvdev_init_begin() {
    struct httpsrvdev_inst inst = {
        .ip = (127<<24) | (0<<16) | (0<<8) | (1<<0),
        .port = 8080,
        .listen_sock_fd = -1,
        .  conn_sock_fd = -1
    };

    return inst;
}

int httpsrvdev_init_end(struct httpsrvdev_inst* inst) {
    inst->listen_sock_addr = (struct sockaddr_in) {
        .sin_family = AF_INET,
        .sin_addr   = { .s_addr = htonl(inst->ip), },
        .sin_port   = htons(inst->port),
    };
    inst->listen_sock_addr_size = sizeof(inst->listen_sock_addr);

    return 1;
}

int httpsrvdev_start(struct httpsrvdev_inst* inst) {
    // Create TCP socket to listen for connections
    inst->listen_sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    // Bind socket to address
    bind(inst->listen_sock_fd,
         (struct sockaddr*) &inst->listen_sock_addr,
         inst->listen_sock_addr_size);
    // Set timeout that the listening socket will be keep around after
    // being closed to 0
    struct linger no_linger = { .l_onoff  = 1, .l_linger = 0 };
    setsockopt(inst->listen_sock_fd,
               SOL_SOCKET, SO_LINGER,
               &no_linger, sizeof(no_linger));
    // Start listening on the socket
    listen(inst->listen_sock_fd, 1);
    printf("Listening...\n");
}

char dummy_req_buf[2048];
int httpsrvdev_res_begin(struct httpsrvdev_inst* inst) {
    inst->conn_sock_fd = accept(
        inst->listen_sock_fd,
        (struct sockaddr*) &inst->listen_sock_addr,
        (socklen_t*)       &inst->listen_sock_addr_size);
    read(inst->conn_sock_fd, dummy_req_buf, 2048);

    return 1;
}

int httpsrvdev_res_end(struct httpsrvdev_inst* inst) {
    close(inst->conn_sock_fd);
    inst->conn_sock_fd = -1;

    return 1;
}

int httpsrvdev_stop(struct httpsrvdev_inst* inst) {
    if (inst->conn_sock_fd != -1) {
        close(inst->conn_sock_fd);
    }
    close(inst->listen_sock_fd);
}

