#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "httpsrvdev_lib.h"

// Resources:
//     HTTP Semantics   : https://datatracker.ietf.org/doc/html/rfc9110
//     HTTP/1.1 RFC 9112: https://datatracker.ietf.org/doc/html/rfc9112

#define SPRINTF_TO_STR_FROM_FMT_AND_VARGS \
    va_list sprintf_args; \
    va_start(sprintf_args, fmt); \
    vsprintf(str_buf, fmt, sprintf_args); \
    va_end(sprintf_args)

#ifdef DEV
    // Utilities for use during development

    void printf_with_escapes(char* fmt, ...) {
        char str_buf[16*1024];
        SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
        for (size_t i = 0; i < strlen(str_buf); ++i) {
            char c = str_buf[i];
            switch (c) {
                case '\n':
                    putchar('\\'); putchar('n'); putchar('\n');
                    break;
                case '\r':
                    putchar('\\'); putchar('r'); putchar('\n');
                    break;
                case '\t':
                    putchar('\\'); putchar('t'); putchar(' '); putchar(' ');
                    break;
                default:
                    putchar(c);
                    break;
            }
        }
        fflush(stdout);
    }
#endif

struct httpsrvdev_inst httpsrvdev_init_begin() {
    struct httpsrvdev_inst inst = {
        .err = httpsrvdev_NO_ERR,

        .ip   = (127<<24) | (0<<16) | (0<<8) | (1<<0),
        .port = 8080,
        .listen_sock_fd = -1,
        .  conn_sock_fd = -1,

        .req_len = 0,
        .req_method = -1,
        .req_target = NULL,
        .req_headers_count = 0,
        .req_body = "",

        .res_status = -1,

        .default_file_mime_type = "\0",

        .root_path = ".",

        // TODO: Memory allignment
        .same_scope_tmp_mem_alloc_offset = 0,
    };
    inst.same_scope_tmp_mem_size = sizeof(inst.same_scope_tmp_mem);

    return inst;
}

static void* same_scope_tmp_alloc(struct httpsrvdev_inst* inst, size_t size) {
    // TODO: Memory allignment
    size_t alloc_end_offset = inst->same_scope_tmp_mem_alloc_offset + size;
    void* ptr;
    // Increment if end of allocation is withing memory region size;
    if (alloc_end_offset < inst->same_scope_tmp_mem_size) {
        ptr = inst->same_scope_tmp_mem + inst->same_scope_tmp_mem_alloc_offset;
        inst->same_scope_tmp_mem_alloc_offset = alloc_end_offset;
    // otherwise reset/wrap to start of region.
    } else {
        ptr = inst->same_scope_tmp_mem;
        inst->same_scope_tmp_mem_alloc_offset = size;
    }

    return ptr;
}

bool httpsrvdev_init_end(struct httpsrvdev_inst* inst) {
    inst->listen_sock_addr = (struct sockaddr_in) {
        .sin_family = AF_INET,
        .sin_addr   = { .s_addr = htonl(inst->ip), },
        .sin_port   = htons(inst->port),
    };
    inst->listen_sock_addr_size = sizeof(inst->listen_sock_addr);

    return true;
}

bool httpsrvdev_start(struct httpsrvdev_inst* inst) {
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

    return true;
}

bool httpsrvdev_stop(struct httpsrvdev_inst* inst) {
    if (inst->conn_sock_fd != -1) {
        close(inst->conn_sock_fd);
    }
    close(inst->listen_sock_fd);

    return true;
}

static bool parse_req(struct httpsrvdev_inst* inst) {
    size_t i = 0;

    // --------------------------------------------------------
    // Parse request's start line
    // --------------------------------------------------------

    // Prase the request method
    inst->req_method_str = inst->req_buf + i;
    if (inst->req_buf[i + 0] == 'G' &&
        inst->req_buf[i + 1] == 'E' &&
        inst->req_buf[i + 2] == 'T'
    ) {
        inst->req_method = httpsrvdev_GET;
        i += 3;
    } else if (
        inst->req_buf[i + 0] == 'P' &&
        inst->req_buf[i + 1] == 'O' &&
        inst->req_buf[i + 2] == 'S' &&
        inst->req_buf[i + 3] == 'T'
    ) {
        inst->req_method = httpsrvdev_POST;
        i += 4;
    } else if (
        inst->req_buf[i + 0] == 'P' &&
        inst->req_buf[i + 1] == 'U' &&
        inst->req_buf[i + 2] == 'T'
    ) {
        inst->req_method = httpsrvdev_PUT;
        i += 3;
    } else if (
        inst->req_buf[i + 0] == 'D' &&
        inst->req_buf[i + 1] == 'E' &&
        inst->req_buf[i + 2] == 'L' &&
        inst->req_buf[i + 3] == 'E' &&
        inst->req_buf[i + 4] == 'T' &&
        inst->req_buf[i + 5] == 'E'
    ) {
        inst->req_method = httpsrvdev_DELETE;
        i += 6;
    } else if (
        inst->req_buf[i + 0] == 'O' &&
        inst->req_buf[i + 1] == 'P' &&
        inst->req_buf[i + 2] == 'T' &&
        inst->req_buf[i + 3] == 'I' &&
        inst->req_buf[i + 4] == 'O' &&
        inst->req_buf[i + 5] == 'N' &&
        inst->req_buf[i + 6] == 'S'
    ) {
        inst->req_method = httpsrvdev_OPTIONS;
        i += 7;
    } else if (
        inst->req_buf[i + 0] == 'H' &&
        inst->req_buf[i + 1] == 'E' &&
        inst->req_buf[i + 2] == 'A' &&
        inst->req_buf[i + 3] == 'D'
    ) {
        inst->req_method = httpsrvdev_HEAD;
        i += 4;
    } else if (
        inst->req_buf[i + 0] == 'C' &&
        inst->req_buf[i + 1] == 'O' &&
        inst->req_buf[i + 2] == 'N' &&
        inst->req_buf[i + 3] == 'N' &&
        inst->req_buf[i + 4] == 'E' &&
        inst->req_buf[i + 5] == 'C' &&
        inst->req_buf[i + 6] == 'T'
    ) {
        inst->req_method = httpsrvdev_CONNECT;
        i += 7;
    } else if (
        inst->req_buf[i + 0] == 'P' &&
        inst->req_buf[i + 1] == 'A' &&
        inst->req_buf[i + 2] == 'T' &&
        inst->req_buf[i + 3] == 'C' &&
        inst->req_buf[i + 4] == 'H'
    ) {
        inst->req_method = httpsrvdev_PATCH;
        i += 5;
    } else if (
        inst->req_buf[i + 0] == 'T' &&
        inst->req_buf[i + 1] == 'R' &&
        inst->req_buf[i + 2] == 'A' &&
        inst->req_buf[i + 3] == 'C' &&
        inst->req_buf[i + 4] == 'E'
    ) {
        inst->req_method = httpsrvdev_TRACE;
        i += 5;
    } else {
        goto parse_err;
    }
    if (inst->req_buf[i] != ' ') {
        goto parse_err;
    }
    inst->req_buf[i++] = '\0';

    // Parse the request target, e.g. the URL to the dev server
    inst->req_target = inst->req_buf + i;
    while (inst->req_buf[i++] != ' ') {}
    inst->req_buf[i - 1] = '\0';

    // As seen above we store `char*` pointers to substrings of `inst->req_buf`
    // in `inst` and manually insert '\0' null terminators to end the substrings.
    // This means that you can directly access a requests target -- normally the
    // page route -- via the `inst->req_target` pointer, e.g.:
    //
    //     char* page_route = inst->req_target;
    //
    // This pattern is repeated for other important request substrings such as
    // header names and values; see below.

    // Parse 'HTTP1.(1|0) '
    if (inst->req_buf[i++] != 'H' ||
        inst->req_buf[i++] != 'T' ||
        inst->req_buf[i++] != 'T' ||
        inst->req_buf[i++] != 'P' ||
        inst->req_buf[i++] != '/' ||
        inst->req_buf[i++] != '1' ||
        inst->req_buf[i++] != '.'
    ) {
        goto parse_err;
    }
    if (inst->req_buf[i] == '1' || inst->req_buf[i] == '0') {
        ++i;
    } else {
        goto parse_err;
    }

    // Parse end of start line
    if (inst->req_buf[i++] != '\r' ||
        inst->req_buf[i++] != '\n'
    ) {
        goto parse_err;
    }

    // --------------------------------------------------------
    // Parse request headers
    // --------------------------------------------------------

    inst->req_headers_count = 0;
    while (inst->req_buf[i] != '\r' && inst->req_buf[i + 1] != '\n') {

        // Parse header name
        inst->req_headers[inst->req_headers_count][0] = inst->req_buf + i;
        while (true) {
            ++i;
            if (inst->req_buf[i] == ':') {
                inst->req_buf[i] = '\0';
                break;
            } else if (inst->req_buf[i] == ' ') {
                inst->req_buf[i] = '\0';
                if (inst->req_buf[++i] != ':') {
                    goto parse_err;
                }
            }
        }
        ++i;

        // Parse header value
        if (inst->req_buf[i++] != ' ') {
            goto parse_err;
        }
        inst->req_headers[inst->req_headers_count][1] = inst->req_buf + i;
        while (inst->req_buf[i++] != '\r') {}
        inst->req_buf[i - 1] = '\0';
        if (inst->req_buf[i++] != '\n') {
            goto parse_err;
        }

        ++inst->req_headers_count;
    }
    i += 2;

    // --------------------------------------------------------
    // Parse request body
    // --------------------------------------------------------

    inst->req_body = inst->req_buf + i;
    if (inst->req_buf[inst->req_len - 2] == '\r' &&
        inst->req_buf[inst->req_len - 1] == '\n'
    ) {
        inst->req_buf[inst->req_len - 2] = '\0';
    } else {
        // TODO: Maybe this case should be an error?
        inst->req_buf[inst->req_len] = '\0';
    }

    // --------------------------------------------------------

    return true;

parse_err:
    inst->err = httpsrvdev_CANNOT_PARSE_REQ;
    return false;
}

bool httpsrvdev_res_begin(struct httpsrvdev_inst* inst) {
    inst->conn_sock_fd = accept(
        inst->listen_sock_fd,
        (struct sockaddr*) &inst->listen_sock_addr,
        (socklen_t*)       &inst->listen_sock_addr_size);

    inst->req_len = recv(inst->conn_sock_fd, inst->req_buf, 2048, 0);
    if (!parse_req(inst)) {
        return false;
    }

    return true;
}

bool httpsrvdev_res_send_n(struct httpsrvdev_inst* inst, char* str, size_t n) {
    if (write(inst->conn_sock_fd, str, n) == -1) {
        // TODO: inst->err = ...
        return false;
    }
    // NOTE: We don't need to do our own buffering becuase TCP sockets are
    //       buffered by default -- see `man 7 tcp`
    return true;
}

bool httpsrvdev_res_send(struct httpsrvdev_inst* inst, char* str) {
    return httpsrvdev_res_send_n(inst, str, strlen(str));
}

bool httpsrvdev_res_end(struct httpsrvdev_inst* inst) {
    if (!httpsrvdev_res_send_n(inst, "\r\n", 2)) return false;

    if (inst->conn_sock_fd != -1) {
        // Flush socket buffer by shutting down write... Not documented in
        // manpage :(
        if (shutdown(inst->conn_sock_fd, SHUT_RDWR) == -1) {
            return false;
        }
        if (close(inst->conn_sock_fd) == -1) {
            return false;
        }
    }
    inst->conn_sock_fd = -1;

    return true;
}

bool httpsrvdev_res_status_line(struct httpsrvdev_inst* inst, int status) {
    inst->res_status = status;

    if (!httpsrvdev_res_send(inst, "HTTP/1.1 ")) return false;
    char* status_buf = same_scope_tmp_alloc(inst, 8);
    sprintf(status_buf, "%d", status);
    if (!httpsrvdev_res_send(inst, status_buf))  return false;
    if (!httpsrvdev_res_send(inst, "\r\n"))      return false;

    return true;
}

bool httpsrvdev_res_header(struct httpsrvdev_inst* inst, char* name, char* value) {
    if (!httpsrvdev_res_send(inst, name  )) return false;
    if (!httpsrvdev_res_send(inst, ": "  )) return false;
    if (!httpsrvdev_res_send(inst, value )) return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;

    return true;
}

bool httpsrvdev_res_headerf(struct httpsrvdev_inst* inst, char* name, char* fmt, ...) {
    char str_buf[1024];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    return httpsrvdev_res_header(inst, name, str_buf);
}

bool httpsrvdev_res_body(struct httpsrvdev_inst* inst, char* body) {
    char* content_len_value_buf = same_scope_tmp_alloc(inst, 24);
    if (!httpsrvdev_res_header(inst, "Content-Length", content_len_value_buf))
        return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;
    if (!httpsrvdev_res_send(inst, body  )) return false;
    if (!httpsrvdev_res_end (inst        )) return false;

    return true;
}

static bool path_rel_to_root_to_path_with_root(struct httpsrvdev_inst* inst,
    char* path, char* result_path
) {
    if (path[0] == '/') {
        strcpy(result_path, path);
        return true;
    }

    // Join root and path
    char* full_path = same_scope_tmp_alloc(inst, 1024);
    char* path_end_ptr = stpcpy(full_path, inst->root_path);
    *(path_end_ptr++) = '/';
    path_end_ptr = stpcpy(path_end_ptr, path);

    // Remove trailing '/'
    if (*(path_end_ptr - 1) == '/') {
        *(path_end_ptr - 1) = '\0';
    }

    realpath(full_path, result_path);

    return true;
}

static bool path_with_root_to_path_rel_to_root(struct httpsrvdev_inst* inst,
    char* path, char* result_path
) {
    char* path_resolved = same_scope_tmp_alloc(inst, 1024);
    path_resolved[0] = '\0';
    realpath(path, path_resolved);

    if (*inst->root_path == '\0') {
        strcpy(result_path, path_resolved);
        return true;
    }

    char* root_path = same_scope_tmp_alloc(inst, 1024);
    realpath(inst->root_path, root_path);

    size_t root_path_len = strlen(root_path);

    if (root_path_len > 0 &&
        strncmp(root_path, path_resolved, root_path_len) != 0
    ) return false;

    strcpy(result_path, path_resolved + root_path_len);

    return true;
}

// TODO: Write a unit test to test that this and the next array match
// MIME Type sources:
//     https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
typedef struct file_type_info {
    uint64_t ext_encoding;
    char*    mime_type;
    bool     charset_utf8;
} FileTypeInfo;
static FileTypeInfo file_type_infos[] = {
    {
        .ext_encoding = ('h'<<24) | ('t'<<16) | ('m'<< 8) | ('l'<< 0),
        .mime_type    = "text/html",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('j'<< 8) | ('s'<< 0),
        .mime_type    = "text/javascript",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<<16) | ('s'<< 8) | ('s'<< 0),
        .mime_type    = "text/css",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('m'<<16) | ('j'<< 8) | ('s'<< 0),
        .mime_type    = "text/javascript",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('h'<<16) | ('t'<< 8) | ('m'<< 0),
        .mime_type    = "text/html",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = (((uint64_t)'x')<<32) |
            ('h'<<24) | ('t'<<16) | ('m'<< 8) | ('l'<< 0),
        .mime_type    = "application/xhtml+xml",
        .charset_utf8 = true,
    },

    // Other common text formats ----------------------------------------
    {
        .ext_encoding = ('j'<<24) | ('s'<<16) | ('o'<< 8) | ('n'<< 0),
        .mime_type    = "application/json",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = (((uint64_t) 'j')<<40) | (((uint64_t)'s')<<32) |
            ('o'<<24) | ('n'<<16) | ('l'<< 8) | ('d'<< 0),
        .mime_type    = "application/ld+json",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('t'<<16) | ('x'<< 8) | ('t'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<<16) | ('s'<< 8) | ('v'<< 0),
        .mime_type    = "text/csv",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('x'<<16) | ('m'<< 8) | ('l'<< 0),
        .mime_type    = "application/xml",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('i'<<16) | ('c'<< 8) | ('s'<< 0),
        .mime_type    = "text/calendar",
        .charset_utf8 = true,
    },

    // Unspecified Binary -----------------------------------------------
    {
        .ext_encoding = ('b'<<16) | ('i'<< 8) | ('n'<< 0),
        .mime_type    = "application/octet-stream",
        .charset_utf8 = true,
    },

    // Images -----------------------------------------------------------
    {
        .ext_encoding = ('j'<<16) | ('p'<< 8) | ('g'<< 0),
        .mime_type    = "image/jpeg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('j'<<24) | ('p'<<16) | ('e'<< 8) | ('g'<< 0),
        .mime_type    = "image/jpeg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('p'<<16) | ('n'<< 8) | ('g'<< 0),
        .mime_type    = "image/png",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('w'<<24) | ('e'<<16) | ('b'<< 8) | ('p'<< 0),
        .mime_type    = "image/webp",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('s'<<16) | ('v'<< 8) | ('g'<< 0),
        .mime_type    = "image/svg+xml",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('g'<<16) | ('i'<< 8) | ('f'<< 0),
        .mime_type    = "image/gif",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('t'<<16) | ('i'<< 8) | ('f'<< 0),
        .mime_type    = "image/tiff",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('t'<<24) | ('i'<<16) | ('f'<< 8) | ('f'<< 0),
        .mime_type    = "image/tiff",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('b'<<16) | ('m'<< 8) | ('p'<< 0),
        .mime_type    = "image/bmp",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<24) | ('p'<<16) | ('n'<< 8) | ('g'<< 0),
        .mime_type    = "image/apng",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('i'<<16) | ('c'<< 8) | ('o'<< 0),
        .mime_type    = "image/vnd.microsoft.icon",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<24) | ('v'<<16) | ('i'<< 8) | ('f'<< 0),
        .mime_type    = "image/avif",
        .charset_utf8 = false,
    },

    // Fonts ------------------------------------------------------------
    {
        .ext_encoding = ('o'<<16) | ('f'<< 8) | ('t'<< 0),
        .mime_type    = "font/oft",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('t'<<16) | ('t'<< 8) | ('f'<< 0),
        .mime_type    = "font/ttf",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('w'<<24) | ('o'<<16) | ('f'<< 8) | ('f'<< 0),
        .mime_type    = "font/woff",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = (((uint64_t) 'w')<<32) |
            ('o'<<24) | ('f'<<16) | ('f'<< 8) | ('2'<< 0),
        .mime_type    = "font/woff2",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('e'<<16) | ('o'<< 8) | ('t'<< 0),
        .mime_type    = "application/vnd.ms-fontobject",
        .charset_utf8 = false,
    },

    // Video ------------------------------------------------------------
    {
        .ext_encoding = ('m'<<16) | ('p'<< 8) | ('4'<< 0),
        .mime_type    = "video/mp4",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('m'<<24) | ('p'<<16) | ('e'<< 8) | ('g'<< 0),
        .mime_type    = "video/mpeg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<16) | ('v'<< 8) | ('i'<< 0),
        .mime_type    = "video/x-msvideo",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('w'<<24) | ('e'<<16) | ('b'<< 8) | ('m'<< 0),
        .mime_type    = "video/webm",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<16) | ('g'<< 8) | ('v'<< 0),
        .mime_type    = "video/ogg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<16) | ('g'<< 8) | ('v'<< 0),
        .mime_type    = "video/mp2t",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('t'<< 8) | ('s'<< 0),
        .mime_type    = "video/mp2t",
        .charset_utf8 = false,
    },

    // Audio ------------------------------------------------------------
    {
        .ext_encoding = ('m'<<16) | ('p'<< 8) | ('3'<< 0),
        .mime_type    = "audio/mpeg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('w'<<16) | ('a'<< 8) | ('v'<< 0),
        .mime_type    = "audio/wav",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<24) | ('p'<<16) | ('u'<< 8) | ('s'<< 0),
        .mime_type    = "audio/opus",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<16) | ('g'<< 8) | ('a'<< 0),
        .mime_type    = "audio/ogg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<16) | ('a'<< 8) | ('c'<< 0),
        .mime_type    = "audio/aac",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('w'<<24) | ('e'<<16) | ('b'<< 8) | ('a'<< 0),
        .mime_type    = "audio/weba",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('m'<<16) | ('i'<< 8) | ('d'<< 0),
        .mime_type    = "audio/midi",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('m'<<24) | ('i'<<16) | ('d'<< 8) | ('i'<< 0),
        .mime_type    = "audio/midi",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('c'<<16) | ('d'<< 8) | ('a'<< 0),
        .mime_type    = "application/x-cdf",
        .charset_utf8 = false,
    },

    // Document / Book formats ------------------------------------------
    {
        .ext_encoding = ('p'<<16) | ('d'<< 8) | ('f'<< 0),
        .mime_type    = "application/pdf",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('d'<<24) | ('o'<<16) | ('c'<< 8) | ('x'<< 0),
        .mime_type    =
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('d'<<16) | ('o'<< 8) | ('c'<< 0),
        .mime_type    = "application/msword",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('x'<<24) | ('l'<<16) | ('s'<< 8) | ('x'<< 0),
        .mime_type    =
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('x'<<16) | ('l'<< 8) | ('s'<< 0),
        .mime_type    = "application/vnd.ms-excel",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('p'<<24) | ('p'<<16) | ('t'<< 8) | ('x'<< 0),
        .mime_type    =
            "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('p'<<16) | ('p'<< 8) | ('t'<< 0),
        .mime_type    = "application/vnd.ms-powerpoint",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<16) | ('d'<< 8) | ('t'<< 0),
        .mime_type    = "application/vnd.oasis.opendocument.text",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<16) | ('d'<< 8) | ('p'<< 0),
        .mime_type    = "application/vnd.oasis.opendocument.presentation",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('o'<<16) | ('d'<< 8) | ('s'<< 0),
        .mime_type    = "application/vnd.oasis.opendocument.spreadsheet",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('e'<<24) | ('p'<<16) | ('u'<< 8) | ('b'<< 0),
        .mime_type    = "application/epub+zip",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<16) | ('z'<< 8) | ('w'<< 0),
        .mime_type    = "application/vnd.amazon.ebook",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<16) | ('b'<< 8) | ('w'<< 0),
        .mime_type    = "application/x-abiword",
        .charset_utf8 = false,
    },

    // Archive / compression formats ------------------------------------
    {
        .ext_encoding = ('z'<<16) | ('i'<< 8) | ('p'<< 0),
        .mime_type    = "application/zip",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('g'<< 8) | ('z'<< 0),
        .mime_type    = "application/gzip",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('t'<<16) | ('a'<< 8) | ('r'<< 0),
        .mime_type    = "application/x-tar",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('7'<< 8) | ('z'<< 0),
        .mime_type    = "application/x-7z-compressed",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('b'<< 8) | ('z'<< 0),
        .mime_type    = "application/x-bzip",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('b'<<16) | ('z'<< 8) | ('2'<< 0),
        .mime_type    = "application/x-bzip2",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('a'<<16) | ('r'<< 8) | ('c'<< 0),
        .mime_type    = "application/x-freearc",
        .charset_utf8 = false,
    },

    // Programmer BS ----------------------------------------------------
    {
        .ext_encoding = ('j'<<16) | ('a'<< 8) | ('r'<< 0),
        .mime_type    = "application/java-archive",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('p'<<16) | ('h'<< 8) | ('p'<< 0),
        .mime_type    = "application/x-httpd-php",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('s'<< 8) | ('h'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('r'<<16) | ('t'<< 8) | ('f'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<<16) | ('s'<< 8) | ('h'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('m'<< 8) | ('d'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('o'<<16) | ('r'<< 8) | ('g'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('a'<<16) | ('d'<<16) | ('o'<< 8) | ('c'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('h'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('p'<< 8) | ('y'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<<16) | ('p'<< 8) | ('p'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<<16) | ('+'<< 8) | ('+'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('h'<<16) | ('p'<< 8) | ('p'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('h'<<16) | ('+'<< 8) | ('+'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('g'<< 8) | ('o'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('j'<<16) | ('a'<<16) | ('v'<< 8) | ('a'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('z'<<16) | ('i'<< 8) | ('g'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('o'<<16) | ('d'<<16) | ('i'<< 8) | ('n'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('h'<< 8) | ('a'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('j'<<16) | ('a'<< 8) | ('i'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('r'<< 8) | ('s'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('c'<<16) | ('j'<< 8) | ('s'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('m'<<16) | ('o'<<16) | ('j'<< 8) | ('o'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },
    {
        .ext_encoding = ('r'<< 8) | ('b'<< 0),
        .mime_type    = "text/plain",
        .charset_utf8 = true,
    },

    // Misc -------------------------------------------------------------
    {
        .ext_encoding = ('o'<<16) | ('g'<< 8) | ('x'<< 0),
        .mime_type    = "application/ogg",
        .charset_utf8 = false,
    },
    {
        .ext_encoding = ('v'<<16) | ('s'<< 8) | ('d'<< 0),
        .mime_type    = "application/vnd.visio",
        .charset_utf8 = false,
    },
};


static FileTypeInfo default_faile_type_info = {
    .ext_encoding = 0,
    .mime_type    = "",
    .charset_utf8 = true,
};

static FileTypeInfo* get_file_type_info_(struct httpsrvdev_inst* inst, char* file_path) {
    uint64_t ext_encoding = httpsrvdev_file_encode_ext(inst, file_path);
    if (ext_encoding == 0) {
        if (inst->default_file_mime_type[0] == '\0') {
            inst->err = httpsrvdev_FILE_HAS_NO_EXT;
        }
        default_faile_type_info.ext_encoding = ext_encoding;
        default_faile_type_info.mime_type    = inst->default_file_mime_type;
        default_faile_type_info.charset_utf8 = true;
        return &default_faile_type_info;
    }

    // TODO: Do something faster than a linear search
    for (size_t i = 0; i < sizeof(file_type_infos)/sizeof(file_type_infos[0]); ++i) {
        if (file_type_infos[i].ext_encoding == ext_encoding) {
            return &file_type_infos[i];
        }
    }

    inst->err = httpsrvdev_LIB_IMPL_ERR;
    default_faile_type_info.ext_encoding = ext_encoding;
    default_faile_type_info.mime_type    = inst->default_file_mime_type;
    default_faile_type_info.charset_utf8 = true;
    return &default_faile_type_info;
}

bool httpsrvdev_res_file(struct httpsrvdev_inst* inst, char* path) {
    FileTypeInfo* file_type_info = get_file_type_info_(inst, path);

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        inst->err = httpsrvdev_COULD_NOT_OPEN_FILE | (errno & httpsrvdev_MASK_ERRNO);
        // Goto ensures that file descriptor is closed
        close(fd);
        return false;
    }

    if (lseek(fd, 0, SEEK_SET) == -1) {
        inst->err = httpsrvdev_FILE_SYS_ERR | (errno & httpsrvdev_MASK_ERRNO);
        close(fd);
        return false;
    }
    int content_length = lseek(fd, 0, SEEK_END);
    if (content_length == -1) {
        inst->err = httpsrvdev_COULD_NOT_GET_FILE_CONTENT_LENGTH |
                    (errno & httpsrvdev_MASK_ERRNO);
        close(fd);
        return false;
    }
    if (lseek(fd, 0, SEEK_SET) == -1) {
        inst->err = httpsrvdev_FILE_SYS_ERR | (errno & httpsrvdev_MASK_ERRNO);
        close(fd);
        return false;
    }

    char content_length_value_buf[20];
    sprintf(content_length_value_buf, "%d", content_length);
    if (!httpsrvdev_res_status_line(inst, 200) ||
        !httpsrvdev_res_header(inst, "Content-Length", content_length_value_buf)
    ) {
        close(fd);
        return false;
    }
    if (file_type_info->charset_utf8) {
        if(!httpsrvdev_res_headerf(inst,
            "Content-Type", "%s; charset=utf-8", file_type_info->mime_type)
        ) {
            close(fd);
            return false;
        }
    } else {
        if(!httpsrvdev_res_header(inst, "Content-Type", file_type_info->mime_type)) {
            close(fd);
            return false;
        }
    }
    if (!httpsrvdev_res_header (inst, "Connection", "Keep-Alive")) {
        close(fd);
        return false;
    }

    if (!httpsrvdev_res_send_n(inst, "\r\n", 2)) return false;

    size_t chunk_size = 2048;
    char   chunk[chunk_size];
    while (true) {
        int n_bytes_read = read(fd, chunk, chunk_size);
        if (n_bytes_read == -1) {
            close(fd);
            return false;
        }
        if (!httpsrvdev_res_send_n(inst, chunk, n_bytes_read)) return false;
        if (n_bytes_read < chunk_size) break;
    }

    if (close(fd) == -1) {
        inst->err = httpsrvdev_COULD_NOT_CLOSE_FILE | (errno & httpsrvdev_MASK_ERRNO);
        return false;
    }

    if (!httpsrvdev_res_end(inst)) return false;

    return true;
}

static char* index_files[] = {"/index.html", "index.htm"};

bool httpsrvdev_res_dir(struct httpsrvdev_inst* inst, char* dir_path) {
    // Respond with the index.htm(l) file of existent in the directory
    for (size_t i = 0; i < sizeof(index_files)/sizeof(index_files[0]); ++i) {
        char* index_file_path = same_scope_tmp_alloc(inst, 1024);
        strcpy(stpcpy(index_file_path, dir_path), index_files[i]);
        struct stat index_file_path_stat;

        if (stat(index_file_path, &index_file_path_stat) == -1 ||
            (index_file_path_stat.st_mode & S_IFMT) != S_IFREG
        ) continue;

        return httpsrvdev_res_file(inst, index_file_path);
    }

    // Construct buffer containing "<dir_path>/" that will act as the prefix
    // for the path to each directory entry
    char entry_path_buf[1024];
    if (!path_with_root_to_path_rel_to_root(inst, dir_path, entry_path_buf))
        return false;
    char* entry_name_in_path_start = entry_path_buf + strlen(entry_path_buf);
    if (*(entry_name_in_path_start - 1) != '/') {
        *entry_name_in_path_start = '/';
        ++entry_name_in_path_start;
    }

    // Create the directory listing
    httpsrvdev_res_listing_begin(inst);
    struct dirent** entries;
    int n_entries = scandir(dir_path, &entries, NULL, alphasort);
    if (n_entries == -1) {
        inst->err = httpsrvdev_COULD_NOT_OPEN_DIR | (errno & httpsrvdev_MASK_ERRNO);
        return false;
    }
    for (size_t i = 0; i < n_entries; ++i) {
        struct dirent* entry = entries[i];
        char* entry_name = entry->d_name;
        char* path_end = stpcpy(entry_name_in_path_start, entry_name);
        // Add trailing '/' to entry path if it's a directory.
        // This ensures that the directory is kept in URL.
        if (entry->d_type == DT_DIR) {
            *(path_end++) = '/';
            *path_end = '\0';
        }
        httpsrvdev_res_listing_entry(inst, entry_path_buf, entry_name);
    }
    httpsrvdev_res_listing_end(inst);

    return true;
}

bool httpsrvdev_res_file_sys_entry(struct httpsrvdev_inst* inst, char* path) {
    struct stat path_stat;
    if (stat(path, &path_stat) == -1) {
        inst->err = httpsrvdev_COULD_NOT_STAT | (errno & httpsrvdev_MASK_ERRNO);
        return false;
    }

    int file_type = path_stat.st_mode & S_IFMT;
    switch (file_type) {
        case S_IFREG:
            return httpsrvdev_res_file(inst, path);
        case S_IFDIR:
            return httpsrvdev_res_dir(inst, path);
    }

    inst->err = httpsrvdev_UNHANDLED_FILE_TYPE | (file_type & httpsrvdev_MASK_FILE_TYPE);
    return false;
}

bool httpsrvdev_res_filef(struct httpsrvdev_inst* inst, char* fmt, ...) {
    char str_buf[512];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    return httpsrvdev_res_file(inst, str_buf);
}

bool httpsrvdev_res_dirf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    char str_buf[512];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    return httpsrvdev_res_dir(inst, str_buf);
}

bool httpsrvdev_res_file_sys_entryf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    char str_buf[512];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    return httpsrvdev_res_file_sys_entry(inst, str_buf);
}

bool httpsrvdev_res_rel_file(struct httpsrvdev_inst* inst, char* path) {
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, path, resolved_path);
    return httpsrvdev_res_file(inst, resolved_path);
}

bool httpsrvdev_res_rel_dir(struct httpsrvdev_inst* inst, char* path) {
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, path, resolved_path);
    return httpsrvdev_res_dir(inst, resolved_path);
}

bool httpsrvdev_res_rel_file_sys_entry(struct httpsrvdev_inst* inst, char* path) {
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, path, resolved_path);
    return httpsrvdev_res_dir(inst, resolved_path);
}

bool httpsrvdev_res_rel_filef(struct httpsrvdev_inst* inst, char* fmt, ...) {
    char str_buf[512];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, str_buf, resolved_path);
    return httpsrvdev_res_file(inst, resolved_path);
}

bool httpsrvdev_res_rel_dirf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    char str_buf[512];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, str_buf, resolved_path);
    return httpsrvdev_res_dir(inst, resolved_path);
}

bool httpsrvdev_res_rel_file_sys_entryf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    char str_buf[512];
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS;
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, str_buf, resolved_path);
    return httpsrvdev_res_file_sys_entry(inst, resolved_path);
}

bool httpsrvdev_res_listing_begin(struct httpsrvdev_inst* inst) {
    httpsrvdev_res_status_line(inst, 200);
    httpsrvdev_res_header(inst, "Content-Type", "text/html");
    httpsrvdev_res_header(inst, "Transfer-Encoding", "chunked");
    httpsrvdev_res_send_n(inst, "\r\n", 2);
    char* chunk = same_scope_tmp_alloc(inst, 256);
    size_t chunk_size = sprintf(chunk,
        "<!DOCTYPE html>\n"
        "<html><body style=\"font-family:sans-serif;\n"
        "background-color:#000;margin:2em\">\n");
    dprintf(inst->conn_sock_fd, "%lX\r\n%s\r\n", chunk_size, chunk);

    return true;
}

bool httpsrvdev_res_listing_entry(struct httpsrvdev_inst* inst,
    char* path, char* link_text
) {
    bool path_is_abs = path[0] == '/';
    char* anchor_target;
    if (path_is_abs) {
        anchor_target = "_top";
    } else {
        anchor_target = "_self";
    }
    char* chunk = same_scope_tmp_alloc(inst, 256);
    size_t chunk_size = sprintf(chunk,
        "<a style=\"color:#FFF;text-decoration:underline;"
                   "display:block;margin-bottom:0.5em\" "
            "href=\"%s\" "
            "target=\"%s\" "
        ">%s</a>",
        path, anchor_target, link_text);
    dprintf(inst->conn_sock_fd, "%lX\r\n%s\r\n", chunk_size, chunk);

    return true;
}

bool httpsrvdev_res_listing_end(struct httpsrvdev_inst* inst) {
    char* chunk = same_scope_tmp_alloc(inst, 256);
    size_t chunk_size = sprintf(chunk,
        "</body></html>");
    dprintf(inst->conn_sock_fd, "%lX\r\n%s\r\n0\r\n", chunk_size, chunk);
    httpsrvdev_res_end(inst);

    return true;
}

uint64_t httpsrvdev_file_encode_ext(struct httpsrvdev_inst* inst, char* file_path) {
    uint64_t encoding = 0;
    size_t path_len = strlen(file_path);
    for (size_t i = 0; i < path_len; ++i) {
        if (i == 8) {
            break;
        }
        char c = file_path[path_len - i - 1];
        if (c == '.') {
            return encoding;
        }
        encoding |= ((uint64_t) c) << (i * 8);
    }

    return 0;
}

bool httpsrvdev_ipv4_from_str(struct httpsrvdev_inst* inst, char* str) {
    int64_t ip = httpsrvdev_ipv4_parse(inst, str);
    if (ip == -1) {
        return false;
    }
    inst->ip = ip;
    return true;
}

int64_t httpsrvdev_ipv4_parse(struct httpsrvdev_inst* inst, char* str) {
    int64_t ip = 0;

    int64_t i = strlen(str);
    for (size_t j = 0; j < 4; ++j) {
        if (--i < 0) goto err_return;

        int64_t byte = 0;
        for (int64_t decimal_exp = 1; decimal_exp < 1001; decimal_exp *= 10) {
            if (decimal_exp > 100 || !('0' <= str[i] && str[i] <= '9'))
                goto err_return;

            byte += decimal_exp*(str[i--] - '0');

            if (byte > 255)             goto err_return;
            if (str[i] == '.' || i < 0) break;
        }
        ip |= (byte << (8*j));
    }

    return ip;

err_return:
    inst->err = httpsrvdev_INVALID_IP;
    return -1;
}

bool httpsrvdev_port_from_str(struct httpsrvdev_inst* inst, char* str) {
    int port = httpsrvdev_port_parse(inst, str);
    if (port == -1) {
        return false;
    }
    inst->port = port;
    return true;
}

int httpsrvdev_port_parse(struct httpsrvdev_inst* inst, char* str) {
    int port = 0;
    int decimal_exp = 1;
    for (int i = strlen(str) - 1; i > -1; --i) {
        if (!('0' <= str[i] && str[i] <= '9')) goto err;

        port += decimal_exp*(str[i] - '0');

        if (port > 0xFFFF)                     goto err;

        decimal_exp *= 10;
    } 

    return port;

err:
    inst->err = httpsrvdev_INVALID_PORT;
    return -1;
}

// General TODOs:
// - Add err_msg to inst and set err_msg at each error site
// - Revaluate use of fixed sized buffers ->
//     1. Could they be too small/large in cases
//     2. Are there better alternatives
// - Replace "while true" loops with bounded loops

// ======================================================================
#ifdef TEST

#include "test_framework.h"

void test_suite(void) {
    test_begin("Example grouping"); {
        test_begin("Test 1."); {
            int a = -1;
            int b = 1;
            test_assert(a == b, "%d != %d", a, b);
        }; test_end();
        test_begin("Test 2."); {
            size_t a = 3;
            size_t b = 4;
            test_assert(a == b, "%zu != %zu", a, b);
        }; test_end();
    }; test_end();
}

int main(void) {
    test_suite();
    if (!test_write_results_and_return_true_on_success(STDOUT_FILENO, STDERR_FILENO))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

#endif // TEST
