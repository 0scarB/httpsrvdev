#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lib.h"

struct httpsrvdev_inst inst;

void handle_sigint() {
    httpsrvdev_stop(&inst);
    exit(0);
}

int main(int argc, char argv[]) {
    signal(SIGINT, handle_sigint);
    inst = httpsrvdev_init_begin();
    httpsrvdev_init_end(&inst);

    httpsrvdev_start(&inst);
    char res_buf[1024];
    while (httpsrvdev_res_begin(&inst)) {
        char* res_content = "<h1>Hello, World!</h1>";
        sprintf(res_buf,
                "HTTP/1.1 200\r\n"
                "Content-Length: %ld\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "%s\r\n",
                strlen(res_content), res_content);
        write(inst.conn_sock_fd, res_buf, 1024);
        httpsrvdev_res_end(&inst);
    }

    return EXIT_SUCCESS;
}

