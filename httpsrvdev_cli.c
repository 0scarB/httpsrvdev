#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "httpsrvdev_lib.h"

#define INFO 1
#define WARN 2
#define ERR  3

char   argv        [16][256];
bool   argv_handled[16];
bool   argv_is_src[16] = {false};
char*  stdin_mime_type = "text/plain";
size_t argv_srcs_count = 0;
size_t argc;
struct httpsrvdev_inst inst;

void unexpected_err_and_exit() {
    perror("Unexpected Implementation Error: "
            "Unexpected log level!");
    exit(1);
}

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
            unexpected_err_and_exit();
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

int argv_find_unhandled_idx(char* short_arg, char* long_arg) {
    // Iterate through args backwards so the latter args override
    // former when duplicates are allowed.
    for (int i = argc - 1; i > -1; --i) {
        if (argv_handled[i]) continue;

        if (strcmp(long_arg, argv[i]) == 0 ||
            (short_arg != NULL && strcmp(short_arg, argv[i]) == 0)
        ) {
            return i;
        }
    }
    return -1;
}

void argv_err_on_duplicate_opts_or_flags(void) {
    char* possible_duplicate_opts[][2] = {
        {NULL, "--ip"        },
        {"-p", "--port"      },
        {"-h", "--help"      },
        {NULL, "--stdin-type"},
        // Not checked: {NULL, "--override-opts"},
    };
    for (size_t i = 0;
         i < sizeof(possible_duplicate_opts)/sizeof(possible_duplicate_opts[0]);
         ++i
    ) {
        char* short_opt = possible_duplicate_opts[i][0];
        char*  long_opt = possible_duplicate_opts[i][1];

        int count = 0;
        for (size_t j = 0; j < argc; ++j) {
            char* arg = argv[j];
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

bool read_stdin(char** buf_ptr, size_t* len) {
    size_t n = 1024*1024;
    *len = n;
    *buf_ptr = malloc(n);
    char* buf = *buf_ptr;

    bool result = false;
    while (n > 0 && fgets(buf, n, stdin)) {
        size_t read_n = strlen(buf);
        buf += read_n;
        n   -= read_n;

        result = true;
    }

    return result;
}

struct httpsrvdev_inst inst;

void handle_sigint() {
    httpsrvdev_stop(&inst);
    exit(0);
}

void res_with_err_page_from_status(size_t status) {
    switch (status) {
        case 404:
            httpsrvdev_res_status_line(&inst, 404);
            httpsrvdev_res_header(&inst, "Content-Type", "text/plain; charset=utf-8");
            httpsrvdev_res_body  (&inst, "File not found!");
            break;
        case 500:
            httpsrvdev_res_status_line(&inst, 500);
            httpsrvdev_res_header(&inst, "Content-Type", "text/plain; charset=utf-8");
            httpsrvdev_res_body  (&inst, "Internal server error!");
            break;
        default:
            unexpected_err_and_exit();
    }
}

void res_with_path_or_err(char* path) {
    if (!httpsrvdev_res_rel_file_sys_entryf(&inst, path)) {
        if ((inst.err & httpsrvdev_MASK_ERRNO) == ENOENT) {
            res_with_err_page_from_status(404);
        } else {
            res_with_err_page_from_status(500);
        }
    }
}

void res_with_stdin(char* stdin_buf) {
    httpsrvdev_res_status_line(&inst, 200);
    httpsrvdev_res_headerf(&inst,
            "Content-Type", "%s; charset=utf-8", stdin_mime_type);
    httpsrvdev_res_body(&inst, stdin_buf);
}

void handle_cli_args() {
    // Determine the executable's name from the first CLI arg
    char* this_exe_name = argv[0];
    argv_handled[0] = true;

    char usage_msg[4096];
    sprintf(usage_msg,
        "%s [OPTIONS/FLAGS] [SRC1 SRC2 ...]\n"
        "\n"
        "Serve files and directories via HTTP.\n"
        "For non-deployment (a.k.a. non-production) software development.\n"
        "\n"
        "[SRC1 SRC2 ...] .... A list of 0 or more sources. A source may\n"
        "                     a) be the path to a file or a directory\n"
        "                     b) be \"-\", as a placeholder for the standard input.\n"
        "                           If 0 sources are provided, the CWD (current \n"
        "                           working directory) will be served.\n"
        "                           If 1 source is is provided, only that source will\n"
        "                           be served.\n"
        "                           If multiple sources are provided, a listing that\n"
        "                           links to each source will be served.\n"
        "                           Paths to directories will serve the HTML page\n"
        "                           'index.html' or 'index.htm' if contained within\n"
        "                           directory; otherwise, a directory listing will be\n"
        "                           served.\n"
        "[OPTIONS/FLAGS]\n"
        "--ip ADDRESS ......... Set the server's IPv4 address. Default \"127.0.0.1\".\n"
        "-p/--port PORT ....... Set the server's port.         Default \"8080\".\n"
        "-h/--help ............ Display this usage message.\n"
        "--stdin-type ......... Set the MIME type that the standard input will be\n"
        "                       served as (if \"-\" is provided as a source).\n"
        "                       Default \"text/plain\".\n"
        "--override-opts ...... Allow the last duplicate of a flag or option to\n"
        "                       override the first. If not provided, duplicates will\n"
        "                       causes an error. When provided this flag additionally\n"
        "                       allows duplicate flags and options to be provided\n"
        "                       after the sources list.\n"
        "                           This flag is useful in scripts when you want to\n"
        "                           use different defaults and still provide the\n"
        "                           ability to override your new defaults from the\n"
        "                           command line, when calling the script.\n"
        "                           E.g. a bash script containing:\n"
        "                                httpsrvdev --port 9000 $@\n"
        "                           allows the caller to override the port:\n"
        "                                $ ./my-script --port 9001\n",
        this_exe_name);

    // Dsiplay the usage message if -h or --help are provided anywhere in the
    // CLI args
    if (argv_find_unhandled_idx("-h", "--help") != -1) {
        puts(usage_msg);
        exit(0);
    }

    // Check for duplicate CLI args if --override-opts is not provided
    int override_opts_flag_idx = argv_find_unhandled_idx(NULL, "--override-opts");
    if (override_opts_flag_idx == -1) {
        argv_err_on_duplicate_opts_or_flags();
    } else {
        argv_handled[override_opts_flag_idx] = true;
    }

    // Check for and handle IPv4 CLI option
    int ip_opt_idx = argv_find_unhandled_idx(NULL, "--ip");
    if (ip_opt_idx != -1) {
        int ip_val_idx = ip_opt_idx + 1;
        if (ip_val_idx >= argc) {
            log_(ERR, "No value provided after --ip!");
            exit(1);
        }
        char* ipv4_addr_str = argv[ip_val_idx];
        if (!httpsrvdev_ipv4_from_str(&inst, ipv4_addr_str)) {
            log_fmt(ERR, "Failed to parse IPv4 address '%s'!", ipv4_addr_str);
            exit(1);
        }
        argv_handled[ip_opt_idx] = true;
        argv_handled[ip_val_idx] = true;
    }

    // Check for and handle port CLI option
    int port_opt_idx = argv_find_unhandled_idx("-p", "--port");
    if (port_opt_idx != -1) {
        int port_val_idx = port_opt_idx + 1;
        if (port_val_idx >= argc) {
            log_fmt(ERR, "No value provided after %s!", argv[port_opt_idx]);
            exit(1);
        }
        char* port_str = argv[port_val_idx];
        if (!httpsrvdev_port_from_str(&inst, port_str)) {
            log_fmt(ERR, "Failed to parse port %s!", port_str);
            exit(1);
        }
        argv_handled[port_opt_idx] = true;
        argv_handled[port_val_idx] = true;
    }

    // Check for and handle stdin MIME type CLI option
    int stdin_mime_type_opt_idx = argv_find_unhandled_idx(NULL, "--stdin-type");
    if (stdin_mime_type_opt_idx != -1) {
        int stdin_mime_type_val_idx = stdin_mime_type_opt_idx + 1;
        if (stdin_mime_type_val_idx >= argc) {
            log_(ERR, "No standard input MIME type value provided after --stdin-type!");
            exit(1);
        }
        stdin_mime_type = argv[stdin_mime_type_val_idx];
        argv_handled[stdin_mime_type_opt_idx] = true;
        argv_handled[stdin_mime_type_val_idx] = true;
    }

    // Assume that remaining unhandled args are sources and check that
    // all sources args are at the end unless --override-opts is provided.
    bool last_was_handled = false;
    if (override_opts_flag_idx == -1) {
        for (int i = argc - 1; i > -1; --i) {
            if (argv_handled[i]) {
                last_was_handled = true;
            } else {
                if (last_was_handled) {
                    log_fmt(ERR, "SRC arguments must all come at the end, "
                                 "after options and flags (unless --override-opts "
                                 "is provided). Non-option/-flag argument "
                                 "'%s' comes before option/flag '%s'!",
                                 argv[i], argv[i + 1]);
                    exit(1);
                }
                argv_is_src[i] = true;
                ++argv_srcs_count;
            }
        }
    } else {
        for (size_t i = 0; i < argc; ++i) {
            if (!argv_handled[i]) {
                argv_is_src[i] = true;
                ++argv_srcs_count;
            }
        }
    }
}

int main(int argc_local, char* argv_local[]) {
    // Populate globals
    for (size_t i = 0; i < argc_local; ++i) {
        strcpy(argv[i], argv_local[i]);
        argv_handled[i] = false;
    }
    argc = argc_local;

    // Initialize httpsrvdev instance from CLI args
    inst = httpsrvdev_init_begin(); {
        handle_cli_args();
    }; httpsrvdev_init_end(&inst);

    signal(SIGINT, handle_sigint);

    // Preprocess sources
    char*  srcs[argv_srcs_count + 1];
    size_t srcs_count = 0;
    char*  stdin_buf = NULL;
    size_t stdin_len = 0;
    for (size_t i = 0; i < argc; ++i) {
        if (argv_is_src[i]) {
            char* src = argv[i];
            bool src_is_stdin = src[0] == '-' && src[1] == '\0';
            if (src_is_stdin) {
                read_stdin(&stdin_buf, &stdin_len);
            }
            srcs[srcs_count++] = src;
        }
    }
    if (srcs_count == 0) {
        srcs[srcs_count++] = ".";
    }

    // Run file server
    httpsrvdev_start(&inst); {
        log_fmt(INFO, "Listening on http://%d.%d.%d.%d:%d...",
                (inst.ip>>24)&0xFF, (inst.ip>>16)&0xFF, (inst.ip>>8)&0xFF, (inst.ip>>0)&0xFF,
                inst.port);

        size_t abs_route_buf_len = 512;
        char   abs_route[abs_route_buf_len];
        char*  rel_route = abs_route + 1;
        // Main loop
        while (true) {
            while (!httpsrvdev_res_begin(&inst)) {
                // Request parsing intermittently fails here when a request finishes
                // after another one has been started -- e.g. when switching back
                // and forth between a page and a directory listing. This is possibly
                // some sort of race condition because we're handling requests
                // synchronously or an implementation error.
                // It hasn't prevented any functionality yet and would probably take
                // a bit of time to diagnose so I'm not going to look into it yet.
                // TODO: Diagnose later
                // TODO: Log warning
            }

            // Set the "route" from the HTTP target
            //    TODO: Add proper target parsing
            //    TODO: HTTP error response when slice is larger than route_buf_len
            strcpy(abs_route, inst.req_target);

            if (srcs_count == 1) {
                bool src_is_stdin = srcs[0][0] == '-' && srcs[0][1] == '\0';
                if (src_is_stdin) {
                    res_with_stdin(stdin_buf);
                } else {
                    char* path = srcs[0];
                    strcpy(inst.root_path, path);
                    res_with_path_or_err(rel_route);
                }
            } else {
                bool is_root_route  = rel_route[0] == '\0';
                bool is_stdin_route = rel_route[0] == '-' && rel_route[1] == '\0';

                if (is_root_route) {
                    httpsrvdev_res_listing_begin(&inst);
                    for (size_t i = 0; i < srcs_count; ++i) {
                        char* src = srcs[i];
                        bool  src_is_stdin = src[0] == '-' && src[1] == '\0';
                        if (src_is_stdin) {
                            httpsrvdev_res_listing_entry(&inst, "-", "STDIN");
                        } else {
                            char* path = src;
                            char resolved_path[512];
                            realpath(path, resolved_path);

                            // Add trailing '/' to resolved path if it's a path
                            // to a directory. This ensures that the path will
                            // be added to the URL
                            struct stat path_stat;
                            if (stat(resolved_path, &path_stat) != -1 &&
                                (path_stat.st_mode & S_IFMT) == S_IFDIR
                            ) {
                                size_t path_len = strlen(resolved_path);
                                resolved_path[path_len    ] = '/';
                                resolved_path[path_len + 1] = '\0';
                            }

                            httpsrvdev_res_listing_entry(&inst, resolved_path, path);
                        }
                    }
                    httpsrvdev_res_listing_end(&inst);
                } else if (is_stdin_route && stdin_buf != NULL) {
                    res_with_stdin(stdin_buf);
                } else {
                    for (size_t i = 0; i < srcs_count; ++i) {
                        char* path = srcs[i];
                        char resolved_path[512];
                        realpath(path, resolved_path);

                        if (strncmp(resolved_path, abs_route, strlen(resolved_path))
                            == 0
                        ) {
                            strcpy(inst.root_path, "");
                            res_with_path_or_err(abs_route);
                            goto main_loop_iter_end;
                        }
                    }

                    res_with_err_page_from_status(404);
                }
            }
        main_loop_iter_end: 
            log_fmt(INFO, "%s %s %d", inst.req_method_str, inst.req_target, inst.res_status);
            continue;
        }
    }; httpsrvdev_stop(&inst);

    return EXIT_FAILURE;
}

