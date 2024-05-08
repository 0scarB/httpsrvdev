#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lib.h"

char   cli_args        [16][256];
bool   cli_args_handled[16];
size_t cli_args_count;
char usage_msg[2048];

int cli_args_find_unhandled_idx(char* arg) {
    for (size_t i = 0; i < cli_args_count; ++i) {
        if (cli_args_handled[i]) continue;

        if (strcmp(arg, cli_args[i]) == 0) {
            return i;
        }
    }
    return -1;
}

struct httpsrvdev_inst inst;

void handle_sigint() {
    httpsrvdev_stop(&inst);
    exit(0);
}

int main(int argc, char* argv[]) {
    for (size_t i = 0; i < argc; ++i) {
        strcpy(cli_args[i], argv[i]);
        cli_args_handled[i] = false;
    }
    cli_args_count = argc;

    char* this_exe_name = cli_args[0];
    cli_args_handled[0] = true;

    sprintf(usage_msg,
            "%s [OPTIONS] [PATH]\n"
            "\n"
            "Serve files and directories via HTTP.\n"
            "For non-deployment (a.k.a. non-production) software development.\n"
            "\n"
            "PATH ............. Path to the directory or file being served.\n"
            "                   Default \".\" / CWD (current working directory).\n"
            "OPTIONS\n"
            "--ip ADDRESS ..... The server's IPv4 address. Default \"127.0.0.1\".\n"
            "-p/--port PORT ... The server's port. Default \"8080\".\n"
            "-h/--help ........ Display this usage message.",
            this_exe_name);

    if (cli_args_find_unhandled_idx("-h"    ) != -1 ||
        cli_args_find_unhandled_idx("--help") != -1
    ) {
        puts(usage_msg);
        return EXIT_SUCCESS;
    }

    inst = httpsrvdev_init_begin();
    signal(SIGINT, handle_sigint);

    // Check for and handle IPv4 CLI option
    int ip_opt_idx = cli_args_find_unhandled_idx("--ip");
    if (ip_opt_idx != -1) {
        int ip_val_idx = ip_opt_idx + 1;
        if (ip_val_idx >= cli_args_count) {
            // TODO: Error message
            return EXIT_FAILURE;
        }
        if (!httpsrvdev_ipv4_from_str(&inst, cli_args[ip_val_idx])) {
            // TODO: Error message
            return EXIT_FAILURE;
        }
        cli_args_handled[ip_opt_idx] = true;
        cli_args_handled[ip_val_idx] = true;
    }

    // Check for and handle port CLI option
    int port_opt_idx = cli_args_find_unhandled_idx("-p");
    if (port_opt_idx == -1) {
        port_opt_idx = cli_args_find_unhandled_idx("--port");
    }
    if (port_opt_idx != -1) {
        int port_val_idx = port_opt_idx + 1;
        if (port_val_idx >= cli_args_count) {
            // TODO: Error message
            return EXIT_FAILURE;
        }
        if (!httpsrvdev_port_from_str(&inst, cli_args[port_val_idx])) {
            // TODO: Error message
            return EXIT_FAILURE;
        }
        cli_args_handled[port_opt_idx] = true;
        cli_args_handled[port_val_idx] = true;
    }

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

        if (argc == 1) {
            char* res_content = "<span>Hello, World!</span>";
            httpsrvdev_res_status_line(&inst, 200);
            httpsrvdev_res_header     (&inst, "Content-Type"  , "text/html");
            httpsrvdev_res_headerf    (&inst, "Content-Length", "%d",
                                              strlen(res_content));
            httpsrvdev_res_body       (&inst, res_content);
        } else if (argc == 2) {
            char* path = argv[1];
            if (!httpsrvdev_res_file_sys_entry(&inst, path)) {
                if (inst.err & httpsrvdev_COULD_NOT_OPEN_FILE) {
                    if ((inst.err & httpsrvdev_MASK_ERRNO) == ENOENT) {
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
        } else if (argc > 2) {
            httpsrvdev_res_listing_begin(&inst);
            for (size_t arg_i = 1; arg_i < argc; ++arg_i) {
                char* path = argv[arg_i];
                httpsrvdev_res_listing_entry(&inst, path, path);
            }
            httpsrvdev_res_listing_end(&inst);
        }
    }

    return EXIT_SUCCESS;
}

