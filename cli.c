#include <errno.h>
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
        for (size_t i = 0; i < inst.req_headers_count; ++i) {
            printf("    Req header name=%s\n",
                    httpsrvdev_req_slice(&inst, &inst.req_header_slices[i][0]));
            printf("    Req header value=%s\n",
                    httpsrvdev_req_slice(&inst, &inst.req_header_slices[i][1]));
        }
        printf("    Req body='%s'\n",
                httpsrvdev_req_slice(&inst, &inst.req_body_slice));
        printf("Req method=%d\n", inst.req_method);
        printf("Req target=%s\n", httpsrvdev_req_slice(&inst, &inst.req_target_slice));

        char* file_path = argv[1];
        if (!httpsrvdev_res_with_file(&inst, file_path)) {
            if (inst.err & httpsrvdev_COULD_NOT_OPEN_FILE) {
                if ((inst.err & httpsrvdev_ERRNO) == ENOENT) {
                    char* res_content = "File not found!";
                    httpsrvdev_res_status_line(&inst, 404);
                    httpsrvdev_res_header     (&inst, "Content-Type"  , "text/plain");
                    httpsrvdev_res_headerf    (&inst, "Content-Length", "%d",
                                                      strlen(res_content));
                    httpsrvdev_res_body       (&inst, res_content);
                } else {
                    char* res_content = "Internal server error!";
                    httpsrvdev_res_status_line(&inst, 500);
                    httpsrvdev_res_header     (&inst, "Content-Type"  , "text/plain");
                    httpsrvdev_res_headerf    (&inst, "Content-Length", "%d",
                                                      strlen(res_content));
                    httpsrvdev_res_body       (&inst, res_content);
                }
            }
        }
    }

    return EXIT_SUCCESS;
}

