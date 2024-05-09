#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lib.h"

#define INFO 1
#define WARN 2
#define ERR  3

char   cli_args        [16][256];
bool   cli_args_handled[16];
size_t cli_args_count;
char usage_msg[2048];

void log_(int log_level, char* msg) {
    FILE* out_file = NULL;
    size_t prefix_len = 20;
    char prefix[prefix_len];
    switch (log_level) {
        case INFO:
            out_file = stdout;
            memcpy(prefix, "(httpsrvdev) INFO : ", prefix_len);
            break;
        case WARN:
            out_file = stderr;
            memcpy(prefix, "(httpsrvdev) WARN : ", prefix_len);
            break;
        case ERR:
            out_file = stderr;
            memcpy(prefix, "(httpsrvdev) ERROR: ", prefix_len);
            break;
        default:
            perror("Unexpected Implementation Error: "
                    "Unexpected log level!");
            exit(1);
    }
    fwrite(prefix, prefix_len, 1, out_file);
    fputs(msg, out_file);
    fputc('\n', out_file);
    fflush(out_file);
}

void log_fmt(int log_level, char* fmt, ...) {
    va_list sprintf_args;
    va_start(sprintf_args, fmt);
    char msg_buf[1024];
    vsprintf(msg_buf, fmt, sprintf_args);
    va_end(sprintf_args);

    log_(log_level, msg_buf);
}

int cli_args_find_unhandled_idx(char* short_arg, char* long_arg) {
    // Iterate through args backwards so the latter args override
    // former when duplicates are allowed.
    for (int i = cli_args_count - 1; i > -1; --i) {
        if (cli_args_handled[i]) continue;

        if (strcmp(long_arg, cli_args[i]) == 0 ||
            (short_arg != NULL && strcmp(short_arg, cli_args[i]) == 0)
        ) {
            return i;
        }
    }
    return -1;
}

void cli_args_err_on_duplicate_opts_or_flags(void) {
    char* possible_duplicate_opts[][2] = {
        {NULL, "--ip"  },
        {"-p", "--port"},
        {"-h", "--help"},
        // Not checked: {NULL, "--override-opts"},
    };
    for (size_t i = 0;
         i < sizeof(possible_duplicate_opts)/sizeof(possible_duplicate_opts[0]);
         ++i
    ) {
        char* short_opt = possible_duplicate_opts[i][0];
        char*  long_opt = possible_duplicate_opts[i][1];

        int count = 0;
        for (size_t j = 0; j < cli_args_count; ++j) {
            char* arg = cli_args[j];
            if (strcmp( long_opt, arg) == 0 ||
                (short_opt != NULL && strcmp(short_opt, arg) == 0)
            ) {
                if (++count > 1) {
                    if (short_opt != NULL) {
                        log_fmt(ERR, "Duplicate option/flag '%s/%s'!", short_opt, long_opt);
                    } else {
                        log_fmt(ERR, "Duplicate option/flag '%s'!", long_opt);
                    }
                    exit(1);
                }
            }
        }
    }
}

struct httpsrvdev_inst inst;

void handle_sigint() {
    httpsrvdev_stop(&inst);
    exit(0);
}

int main(int argc, char* argv[]) {
    // Populate globals
    for (size_t i = 0; i < argc; ++i) {
        strcpy(cli_args[i], argv[i]);
        cli_args_handled[i] = false;
    }
    cli_args_count = argc;

    // Determine the executable's name from the first CLI arg
    char* this_exe_name = cli_args[0];
    cli_args_handled[0] = true;

    sprintf(usage_msg,
            "%s [OPTIONS/FLAGS] [PATH]\n"
            "\n"
            "Serve files and directories via HTTP.\n"
            "For non-deployment (a.k.a. non-production) software development.\n"
            "\n"
            "[PATH] ........... Set the path to the directory or file being served.\n"
            "                   Default \".\" / CWD (current working directory).\n"
            "[OPTIONS/FLAGS]\n"
            "--ip ADDRESS ..... Set the server's IPv4 address. Default \"127.0.0.1\".\n"
            "-p/--port PORT ... Set the server's port. Default \"8080\".\n"
            "-h/--help ........ Display this usage message.\n"
            "--override-opts .. Allow the last duplicate or a flag or option to\n"
            "                   override the first. If not provided, duplicates will\n"
            "                   causes an error.\n"
            "                       This flag is useful in scripts when you want to\n"
            "                       use different defaults and still provide the\n"
            "                       ability to override your new defaults from the\n"
            "                       command line, when calling the script.\n"
            "                       E.g. a bash script containing:\n"
            "                            httpsrvdev --port 9000 $@\n"
            "                       allows the caller to override the port:\n"
            "                            $ ./my-script --port 9001",
            this_exe_name);

    // Dsiplay the usage message if -h or --help are provided anywhere in the
    // CLI args
    if (cli_args_find_unhandled_idx("-h", "--help") != -1) {
        puts(usage_msg);
        return EXIT_SUCCESS;
    }

    // Check for duplicate CLI args if --override-opts is not provided
    int override_opts_flag_idx = cli_args_find_unhandled_idx(NULL, "--override-opts");
    if (override_opts_flag_idx == -1) {
        cli_args_err_on_duplicate_opts_or_flags();
    } else {
        cli_args_handled[override_opts_flag_idx] = true;
    }

    inst = httpsrvdev_init_begin();
    signal(SIGINT, handle_sigint);

    // Check for and handle IPv4 CLI option
    int ip_opt_idx = cli_args_find_unhandled_idx(NULL, "--ip");
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
    int port_opt_idx = cli_args_find_unhandled_idx("-p", "--port");
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

