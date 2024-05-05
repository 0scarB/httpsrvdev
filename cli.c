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

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_sigint);
    inst = httpsrvdev_init_begin();
    httpsrvdev_init_end(&inst);

    httpsrvdev_start(&inst);
    while (httpsrvdev_res_begin(&inst)) {
        httpsrvdev_res_status_line(&inst, 200);
        httpsrvdev_res_header     (&inst, "Content-Type", "text/html");
        httpsrvdev_res_body       (&inst, "<h1>Hello, World!</h1>");

        printf("Req method=%d\n", inst.req_method);
        printf("Req target=%s\n", httpsrvdev_req_slice(&inst, &inst.req_target_slice));
        for (size_t i = 0; i < inst.req_headers_count; ++i) {
            printf("    Req header name=%s\n",
                    httpsrvdev_req_slice(&inst, &inst.req_header_slices[i][0]));
            printf("    Req header value=%s\n",
                    httpsrvdev_req_slice(&inst, &inst.req_header_slices[i][1]));
        }
        printf("    Req body='%s'\n",
                httpsrvdev_req_slice(&inst, &inst.req_body_slice));
        httpsrvdev_res_end(&inst);
    }

    return EXIT_SUCCESS;
}

