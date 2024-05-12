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
char usage_msg[4096];

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
        "%s [OPTIONS/FLAGS] [PATH1 PATH2 ...]\n"
        "\n"
        "Serve files and directories via HTTP.\n"
        "For non-deployment (a.k.a. non-production) software development.\n"
        "\n"
        "[PATH1 PATH2 ...] .... A list of 0 or more PATHs to files or directories to\n"
        "                       serve.\n"
        "                           If 0 PATHs are provided, the CWD (current working\n"
        "                           directory) will be served.\n"
        "                           If 1 PATH is is provided, only that PATH will be\n"
        "                           served.\n"
        "                           If multiple PATHs are provided, a listing that\n"
        "                           links to each PATH will be served.\n"
        "                           Paths to directories will serve the HTML page\n"
        "                           'index.html' or 'index.htm' if contained within\n"
        "                           directory; otherwise, a directory listing will be\n"
        "                           served.\n"
        "[OPTIONS/FLAGS]\n"
        "--ip ADDRESS ......... Set the server's IPv4 address. Default \"127.0.0.1\".\n"
        "-p/--port PORT ....... Set the server's port.         Default \"8080\".\n"
        "-h/--help ............ Display this usage message.\n"
        "--override-opts ...... Allow the last duplicate of a flag or option to\n"
        "                       override the first. If not provided, duplicates will\n"
        "                       causes an error. When provided this flag additionally\n"
        "                       allows duplicate flags and options to be provided\n"
        "                       after the PATHs list.\n"
        "                           This flag is useful in scripts when you want to\n"
        "                           use different defaults and still provide the\n"
        "                           ability to override your new defaults from the\n"
        "                           command line, when calling the script.\n"
        "                           E.g. a bash script containing:\n"
        "                                httpsrvdev --port 9000 $@\n"
        "                           allows the caller to override the port:\n"
        "                                $ ./my-script --port 9001",
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

    // Assume that remaining unhandled args are PATHs and check that
    // all path args are at the end unless --override-opts is provided.
    bool   cli_args_is_path[sizeof(cli_args)/sizeof(cli_args[0])];
    size_t cli_args_paths_count = 0;
    bool last_was_handled = false;
    if (override_opts_flag_idx == -1) {
        for (int i = cli_args_count - 1; i > -1; --i) {
            if (cli_args_handled[i]) {
                last_was_handled = true;
            } else {
                if (last_was_handled) {
                    log_fmt(ERR, "PATH arguments must all come at the end, "
                                 "after options and flags (unless --override-opts "
                                 "is provided). Non-option/-flag argument "
                                 "'%s' comes before option/flag '%s'!",
                                 cli_args[i], cli_args[i + 1]);
                    return EXIT_FAILURE;
                }
                cli_args_is_path[i] = true;
                ++cli_args_paths_count;
            }
        }
    } else {
        for (size_t i = 0; i < cli_args_count; ++i) {
            if (!cli_args_handled[i]) {
                cli_args_is_path[i] = true;
                ++cli_args_paths_count;
            }
        }
    }

    httpsrvdev_start(&inst);
    log_fmt(INFO, "Listening...");

    size_t route_buf_len = 512;
    char   route_buf[route_buf_len];
    if (cli_args_paths_count < 2) {
        if (cli_args_paths_count == 1) {
            for (size_t i = 0; i < cli_args_count; ++i) {
                if (cli_args_is_path[i]) {
                    char* path = cli_args[i];
                    if (path[0] == '/') {
                        strcpy(inst.root_path, "/");
                    } else {
                        strcpy(inst.root_path, path);
                    }
                    break;
                }
            }
        }
        while (httpsrvdev_res_begin(&inst)) {
            // TODO: Add proper target parsing
            // TODO: HTTP error response when slice is larger than route_buf_len
            httpsrvdev_req_slice_copy_to_buf(
                    &inst, &inst.req_target_slice,
                    route_buf, route_buf_len);
            char* path;
            if (route_buf[0] == '/') {
                path = route_buf + 1;
            } else {
                path = route_buf;
            }

            if (!httpsrvdev_res_rel_file_sys_entryf(&inst, path)) {
                if ((inst.err & httpsrvdev_MASK_ERRNO) == ENOENT) {
                    httpsrvdev_res_status_line(&inst, 404);
                    httpsrvdev_res_header(&inst, "Content-Type", "text/plain");
                    httpsrvdev_res_body  (&inst, "File not found!");
                } else {
                    httpsrvdev_res_status_line(&inst, 500);
                    httpsrvdev_res_header(&inst, "Content-Type", "text/plain");
                    httpsrvdev_res_body  (&inst, "Internal server error!");
                }
            }
        }
    } else {
        char* set_root_path_route_prefix = "set_root_";
        bool  root_path_is_set = false;

        while (httpsrvdev_res_begin(&inst)) {
            // TODO: Add proper target parsing
            // TODO: HTTP error response when slice is larger than route_buf_len
            httpsrvdev_req_slice_copy_to_buf(
                    &inst, &inst.req_target_slice,
                    route_buf, route_buf_len);
            char* rel_route;
            if (route_buf[0] == '/') {
                rel_route = route_buf + 1;
            } else {
                rel_route = route_buf;
            }

            if (root_path_is_set) {
                if (!httpsrvdev_res_rel_file_sys_entryf(&inst, rel_route)) {
                    if ((inst.err & httpsrvdev_MASK_ERRNO) == ENOENT) {
                        httpsrvdev_res_status_line(&inst, 404);
                        httpsrvdev_res_header(&inst, "Content-Type", "text/plain");
                        httpsrvdev_res_body  (&inst, "File not found!");
                    } else {
                        httpsrvdev_res_status_line(&inst, 500);
                        httpsrvdev_res_header(&inst, "Content-Type", "text/plain");
                        httpsrvdev_res_body  (&inst, "Internal server error!");
                    }
                }
            } else if (rel_route[0] == '\0') {
                httpsrvdev_res_listing_begin(&inst);
                for (size_t i = 0; i < cli_args_count; ++i) {
                    if (!cli_args_is_path[i]) continue;

                    char* path = cli_args[i];
                    char  link[512];
                    sprintf(link, "%s%s", set_root_path_route_prefix, path);
                    httpsrvdev_res_listing_entry(&inst, link, path);
                }
                httpsrvdev_res_listing_end(&inst);
            } else if (strcmp(rel_route, set_root_path_route_prefix) >= 0) {
                char* root_path = rel_route + strlen(set_root_path_route_prefix);
                strcpy(inst.root_path, root_path);
                root_path_is_set = true;

                httpsrvdev_res_status_line(&inst, 303);
                httpsrvdev_res_header     (&inst, "Location", "/");
                httpsrvdev_res_end        (&inst);
            } else {
                httpsrvdev_res_status_line(&inst, 404);
                httpsrvdev_res_header(&inst, "Content-Type", "text/plain");
                httpsrvdev_res_body  (&inst, "File not found!");
            }
        }
    }

    return EXIT_SUCCESS;
}

