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
#include "lib.h"

// Resources:
//     HTTP Semantics   : https://datatracker.ietf.org/doc/html/rfc9110
//     HTTP/1.1 RFC 9112: https://datatracker.ietf.org/doc/html/rfc9112

#define SPRINTF_TO_STR_FROM_FMT_AND_VARGS(str_buf_size) \
    va_list sprintf_args; \
    va_start(sprintf_args, fmt); \
    char str[str_buf_size]; \
    vsprintf(str, fmt, sprintf_args); \
    va_end(sprintf_args)

#ifdef DEV
    // Utilities for use during development

    void printf_with_escapes(char* fmt, ...) {
        SPRINTF_TO_STR_FROM_FMT_AND_VARGS(16*1024);
        for (size_t i = 0; i < strlen(str); ++i) {
            char c = str[i];
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

        .res_chunk_len = 0,

        .default_file_mime_type = "\0",

        .root_path = ".",
    };

    return inst;
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

bool httpsrvdev_req_slice_copy_to_buf(struct httpsrvdev_inst* inst,
    uint16_t(*slice)[2],
    char*  dst_buf,
    size_t dst_buf_len
) {
    size_t len = (*slice)[1];
    if (len > dst_buf_len - 1) {
        inst->err = httpsrvdev_BUF_TOO_SMALL;
        return false;
    }

    strncpy(dst_buf, inst->req_buf + (*slice)[0], len - 1);
    dst_buf[len - 1] = '\0';
    return true;
}

static bool parse_req(struct httpsrvdev_inst* inst) {
    size_t i = 0;

    // --------------------------------------------------------
    // Parse request's start line
    // --------------------------------------------------------

    // Prase the request method
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
    if (inst->req_buf[i++] != ' ') {
        goto parse_err;
    }

    // Parse the request target, e.g. the URL to the dev server
    inst->req_target_slice[0] = i;
    while (inst->req_buf[i++] != ' ') {}
    inst->req_target_slice[1] = i - inst->req_target_slice[0];

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
        inst->req_header_slices[inst->req_headers_count][0][0] = i;
        while (true) {
            ++i;
            if (inst->req_buf[i] == ':') {
                inst->req_header_slices[inst->req_headers_count][0][1] = i;
                break;
            } else if (inst->req_buf[i] == ' ') {
                inst->req_header_slices[inst->req_headers_count][0][1] = i;
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
        inst->req_header_slices[inst->req_headers_count][1][0] = i;
        while (inst->req_buf[i++] != '\r') {}
        inst->req_header_slices[inst->req_headers_count][1][1] = i;
        if (inst->req_buf[i++] != '\n') {
            goto parse_err;
        }

        ++inst->req_headers_count;
    }
    i += 2;

    // --------------------------------------------------------
    // Parse request body
    // --------------------------------------------------------

    inst->req_body_slice[0] = i;
    inst->req_body_slice[1] = inst->req_len;

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
    // Naive buffered I/O
    if (n + inst->res_chunk_len <= 2048) {
        strncpy(inst->res_chunk_buf + inst->res_chunk_len, str, n);
        inst->res_chunk_len += n;
        return true;
    } else {
        int copy_n_bytes = 2048 - inst->res_chunk_len;
        strncpy(inst->res_chunk_buf + inst->res_chunk_len, str, copy_n_bytes);
        write(inst->conn_sock_fd, inst->res_chunk_buf, 2048);
        inst->res_chunk_len = 0;

        if (!httpsrvdev_res_send_n(inst, str + copy_n_bytes, n - copy_n_bytes))
            return false;
    }

    return true;
}

bool httpsrvdev_res_send(struct httpsrvdev_inst* inst, char* str) {
    return httpsrvdev_res_send_n(inst, str, strlen(str));
}

bool httpsrvdev_res_end(struct httpsrvdev_inst* inst) {
    if (inst->res_chunk_buf[inst->res_chunk_len - 2] != '\r' ||
        inst->res_chunk_buf[inst->res_chunk_len - 1] != '\n'
    ) {
        httpsrvdev_res_send_n(inst, "\r\n", 2);
    }

    if (inst->res_chunk_len > 0) {
        inst->res_chunk_buf[inst->res_chunk_len] = '\0';
        write(inst->conn_sock_fd, inst->res_chunk_buf, inst->res_chunk_len);
        inst->res_chunk_len = 0;
    }

    if (inst->conn_sock_fd != -1) {
        // Shutdown seems to be required before close for large files
        // that require multiple write calls.
        // TODO: Investigate why. Is this intended behaviour? Is it bad for performance?
        shutdown(inst->conn_sock_fd, SHUT_RDWR);
        close(inst->conn_sock_fd);
    }
    inst->conn_sock_fd = -1;

    return true;
}

bool httpsrvdev_res_status_line(struct httpsrvdev_inst* inst, int status) {
    if (!httpsrvdev_res_send(inst, "HTTP/1.1 ")) return false;
    char status_buf[4];
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
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    return httpsrvdev_res_header(inst, name, str);
}

bool httpsrvdev_res_body(struct httpsrvdev_inst* inst, char* body) {
    char content_len_value_buf[20];
    sprintf(content_len_value_buf, "%ld", strlen(body));
    if (!httpsrvdev_res_header(inst, "Content-Length", content_len_value_buf))
        return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;
    if (!httpsrvdev_res_send(inst, body  )) return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;

    if (!httpsrvdev_res_end(inst)) return false;

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
    char full_path[strlen(path) + strlen(inst->root_path) + 2];
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
    char path_resolved[1024];
    path_resolved[0] = '\0';
    realpath(path, path_resolved);

    if (*inst->root_path == '\0') {
        strcpy(result_path, path_resolved);
        return true;
    }

    char root_path[1024];
    realpath(inst->root_path, root_path);

    size_t root_path_len = strlen(root_path);

    if (root_path_len > 0 &&
        strncmp(root_path, path_resolved, root_path_len) != 0
    ) return false;

    strcpy(result_path, path_resolved + root_path_len);

    return true;
}

bool httpsrvdev_res_file(struct httpsrvdev_inst* inst, char* path) {
    char* mime_type = httpsrvdev_file_mime_type(inst, path);

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
    if (!httpsrvdev_res_status_line(inst, 200                                  ) ||
        !httpsrvdev_res_header(inst, "Content-Type"  , mime_type               ) ||
        !httpsrvdev_res_header(inst, "Content-Length", content_length_value_buf) ||
        !httpsrvdev_res_header(inst, "Connection"    , "Keep-Alive")
    ) {
        close(fd);
        return false;
    }

    // TODO: Read and send in chunks
    char file_content_buf[content_length];
    if (read(fd, file_content_buf, content_length) == -1) {
        inst->err = httpsrvdev_COULD_NOT_READ_FILE | (errno & httpsrvdev_MASK_ERRNO);
        close(fd);
        return false;
    }

    if (close(fd) == -1) {
        inst->err = httpsrvdev_COULD_NOT_CLOSE_FILE | (errno & httpsrvdev_MASK_ERRNO);
        return false;
    }

    if (!httpsrvdev_res_send_n(inst, "\r\n"          , 2             )) return false;
    if (!httpsrvdev_res_send_n(inst, file_content_buf, content_length)) return false;

    if (!httpsrvdev_res_end(inst)) return false;

    return true;
}

bool httpsrvdev_res_dir(struct httpsrvdev_inst* inst, char* dir_path) {
    char entry_path_buf[1024];
    if (!path_with_root_to_path_rel_to_root(inst, dir_path, entry_path_buf))
        return false;
    char* entry_name_in_path_start = entry_path_buf + strlen(entry_path_buf);
    if (*(entry_name_in_path_start - 1) != '/') {
        *entry_name_in_path_start = '/';
        ++entry_name_in_path_start;
    }

    httpsrvdev_res_listing_begin(inst);
    struct dirent** entries;
    int n_entries = scandir(dir_path, &entries, NULL, alphasort);
    if (n_entries == -1) {
        inst->err = httpsrvdev_COULD_NOT_OPEN_DIR | (errno & httpsrvdev_MASK_ERRNO);
        return false;
    }
    for (size_t i = 0; i < n_entries; ++i) {
        char* entry_name = entries[i]->d_name;
        strcpy(entry_name_in_path_start, entry_name);
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
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    return httpsrvdev_res_file(inst, str);
}

bool httpsrvdev_res_dirf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    return httpsrvdev_res_dir(inst, str);
}

bool httpsrvdev_res_file_sys_entryf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    return httpsrvdev_res_file_sys_entry(inst, str);
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
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, str, resolved_path);
    return httpsrvdev_res_file(inst, resolved_path);
}

bool httpsrvdev_res_rel_dirf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, str, resolved_path);
    return httpsrvdev_res_dir(inst, resolved_path);
}

bool httpsrvdev_res_rel_file_sys_entryf(struct httpsrvdev_inst* inst, char* fmt, ...) {
    SPRINTF_TO_STR_FROM_FMT_AND_VARGS(512);
    char resolved_path[512];
    path_rel_to_root_to_path_with_root(inst, str, resolved_path);
    return httpsrvdev_res_file_sys_entry(inst, resolved_path);
}

bool httpsrvdev_res_listing_begin(struct httpsrvdev_inst* inst) {
    inst->listing_res_content_len = 0;
    memcpy(inst->listing_res_content + inst->listing_res_content_len,
            "<!DOCTYPE html>\n"
            "<html><body style=\"background-color:#000;margin:2em\">\n", 70);
    inst->listing_res_content_len += 70;

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
    inst->listing_res_content_len +=
        sprintf(inst->listing_res_content + inst->listing_res_content_len,
            "<a style=\"color:#FFF;text-decoration:underline;"
                       "display:block;margin-bottom:0.5em\" "
                "href=\"%s\" "
                "target=\"%s\" "
            ">%s</a>",
            path,
            anchor_target,
            link_text
        );

    return true;
}

bool httpsrvdev_res_listing_end(struct httpsrvdev_inst* inst) {
    memcpy(inst->listing_res_content + inst->listing_res_content_len, "</body></html>", 14);
    inst->listing_res_content_len += 14;
    inst->listing_res_content[inst->listing_res_content_len] = '\0';

    if (!httpsrvdev_res_status_line(inst, 200                        )) return false;
    if (!httpsrvdev_res_header     (inst, "Content-Type", "text/html")) return false;
    if (!httpsrvdev_res_body       (inst, inst->listing_res_content  )) return false;

    return true;
}

// TODO: Write a unit test to test that this and the next array match
// MIME Type sources:
//     https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
static uint64_t ext_encodings[] = {
    // Web Stuff --------------------------------------------------------
                            ('h'<<24) | ('t'<<16) | ('m'<< 8) | ('l'<< 0),
                                                    ('j'<< 8) | ('s'<< 0),
                                        ('c'<<16) | ('s'<< 8) | ('s'<< 0),
                                        ('m'<<16) | ('j'<< 8) | ('s'<< 0),
                                        ('h'<<16) | ('t'<< 8) | ('m'<< 0),
                ('x'<<24) | ('h'<<24) | ('t'<<16) | ('m'<< 8) | ('l'<< 0),

    // Other common text formats ----------------------------------------
                            ('j'<<24) | ('s'<<16) | ('o'<< 8) | ('n'<< 0),
    (((uint64_t) 'j')<<40) | (((uint64_t)'s')<<32)|
                            ('o'<<24) | ('n'<<16) | ('l'<< 8) | ('d'<< 0),
                                        ('t'<<16) | ('x'<< 8) | ('t'<< 0),
                                        ('c'<<16) | ('s'<< 8) | ('v'<< 0),
                                        ('x'<<16) | ('m'<< 8) | ('l'<< 0),
                                        ('i'<<16) | ('c'<< 8) | ('s'<< 0),

    // Binary -----------------------------------------------------------
                                        ('b'<<16) | ('i'<< 8) | ('n'<< 0),

    // Images -----------------------------------------------------------
                                        ('j'<<16) | ('p'<< 8) | ('g'<< 0),
                            ('j'<<24) | ('p'<<16) | ('e'<< 8) | ('g'<< 0),
                                        ('p'<<16) | ('n'<< 8) | ('g'<< 0),
                            ('w'<<24) | ('e'<<16) | ('b'<< 8) | ('p'<< 0),
                                        ('s'<<16) | ('v'<< 8) | ('g'<< 0),
                                        ('g'<<16) | ('i'<< 8) | ('f'<< 0),
                                        ('t'<<16) | ('i'<< 8) | ('f'<< 0),
                            ('t'<<24) | ('i'<<16) | ('f'<< 8) | ('f'<< 0),
                                        ('b'<<16) | ('m'<< 8) | ('p'<< 0),
                            ('a'<<24) | ('p'<<16) | ('n'<< 8) | ('g'<< 0),
                                        ('i'<<16) | ('c'<< 8) | ('o'<< 0),
                            ('a'<<24) | ('v'<<16) | ('i'<< 8) | ('f'<< 0),

    // Fonts ------------------------------------------------------------
                                        ('o'<<16) | ('f'<< 8) | ('t'<< 0),
                                        ('t'<<16) | ('t'<< 8) | ('f'<< 0),
                            ('w'<<24) | ('o'<<16) | ('f'<< 8) | ('f'<< 0),
                ('w'<<24) | ('o'<<24) | ('f'<<16) | ('f'<< 8) | ('2'<< 0),
                                        ('e'<<16) | ('o'<< 8) | ('t'<< 0),

    // Video ------------------------------------------------------------
                                        ('m'<<16) | ('p'<< 8) | ('4'<< 0),
                            ('m'<<24) | ('p'<<16) | ('e'<< 8) | ('g'<< 0),
                                        ('a'<<16) | ('v'<< 8) | ('i'<< 0),
                            ('w'<<24) | ('e'<<16) | ('b'<< 8) | ('m'<< 0),
                                        ('o'<<16) | ('g'<< 8) | ('v'<< 0),
                                                    ('t'<< 8) | ('s'<< 0),

    // Audio ------------------------------------------------------------
                                        ('m'<<16) | ('p'<< 8) | ('3'<< 0),
                                        ('w'<<16) | ('a'<< 8) | ('v'<< 0),
                            ('o'<<24) | ('p'<<16) | ('u'<< 8) | ('s'<< 0),
                                        ('o'<<16) | ('g'<< 8) | ('a'<< 0),
                                        ('a'<<16) | ('a'<< 8) | ('c'<< 0),
                            ('w'<<24) | ('e'<<16) | ('b'<< 8) | ('a'<< 0),
                                        ('m'<<16) | ('i'<< 8) | ('d'<< 0),
                            ('m'<<24) | ('i'<<16) | ('d'<< 8) | ('i'<< 0),
                                        ('c'<<16) | ('d'<< 8) | ('a'<< 0),

    // Document / Book formats ------------------------------------------
                                        ('p'<<16) | ('d'<< 8) | ('f'<< 0),
                            ('d'<<24) | ('o'<<16) | ('c'<< 8) | ('x'<< 0),
                                        ('d'<<16) | ('o'<< 8) | ('c'<< 0),
                            ('x'<<24) | ('l'<<16) | ('s'<< 8) | ('x'<< 0),
                                        ('x'<<16) | ('l'<< 8) | ('s'<< 0),
                            ('p'<<24) | ('p'<<16) | ('t'<< 8) | ('x'<< 0),
                                        ('p'<<16) | ('p'<< 8) | ('t'<< 0),
                                        ('o'<<16) | ('d'<< 8) | ('t'<< 0),
                                        ('o'<<16) | ('d'<< 8) | ('p'<< 0),
                                        ('o'<<16) | ('d'<< 8) | ('s'<< 0),
                            ('e'<<24) | ('p'<<16) | ('u'<< 8) | ('b'<< 0),
                                        ('a'<<16) | ('z'<< 8) | ('w'<< 0),
                                        ('a'<<16) | ('b'<< 8) | ('w'<< 0),

    // Archive / compression formats ------------------------------------
                                        ('z'<<16) | ('i'<< 8) | ('p'<< 0),
                                                    ('g'<< 8) | ('z'<< 0),
                                        ('t'<<16) | ('a'<< 8) | ('r'<< 0),
                                                    ('7'<< 8) | ('z'<< 0),
                                                    ('b'<< 8) | ('z'<< 0),
                                        ('b'<<16) | ('z'<< 8) | ('2'<< 0),
                                        ('a'<<16) | ('r'<< 8) | ('c'<< 0),

    // Programmer BS ----------------------------------------------------
                                        ('j'<<16) | ('a'<< 8) | ('r'<< 0),
                                        ('p'<<16) | ('h'<< 8) | ('p'<< 0),
                                                    ('s'<< 8) | ('h'<< 0),
                                        ('r'<<16) | ('t'<< 8) | ('f'<< 0),
                                        ('c'<<16) | ('s'<< 8) | ('h'<< 0),

    // Other programmer extensions are treated as text.
    // TODO: Make this togglable because this is useful but also unexpected
    //       behaviour
                            // Markdown
                                                    ('m'<< 8) | ('d'<< 0),
                            // Org mode
                                        ('o'<<16) | ('r'<< 8) | ('g'<< 0),
                            // Ascii Doc
                            ('a'<<16) | ('d'<<16) | ('o'<< 8) | ('c'<< 0),
                            // C
                                                                ('c'<< 0),
                                                                ('h'<< 0),
                            // Python
                                                    ('p'<< 8) | ('y'<< 0),
                            // C++
                                        ('c'<<16) | ('p'<< 8) | ('p'<< 0),
                                        ('c'<<16) | ('+'<< 8) | ('+'<< 0),
                                        ('h'<<16) | ('p'<< 8) | ('p'<< 0),
                                        ('h'<<16) | ('+'<< 8) | ('+'<< 0),
                            // Go
                                                    ('g'<< 8) | ('o'<< 0),
                            // Java
                            ('j'<<16) | ('a'<<16) | ('v'<< 8) | ('a'<< 0),
                            // Zig
                                        ('z'<<16) | ('i'<< 8) | ('g'<< 0),
                            // Odin
                            ('o'<<16) | ('d'<<16) | ('i'<< 8) | ('n'<< 0),
                            // Odin
                                                    ('h'<< 8) | ('a'<< 0),
                            // Jai
                                        ('j'<<16) | ('a'<< 8) | ('i'<< 0),
                            // JavaScript -- Node's CommonJS
                                        ('c'<<16) | ('j'<< 8) | ('s'<< 0),
                            // Mojo
                            ('m'<<16) | ('o'<<16) | ('j'<< 8) | ('o'<< 0),
                                // NOTE: We're not going to support
                                //       emoji extensions unless they
                                //       become common... It's just
                                //       cringe marketing that makes
                                //       building software harder
                            // Ruby
                                                    ('r'<< 8) | ('b'<< 0),
                            // This list is non-exhaustive -- just stuff
                            // that I might use. Feel free to extend /
                            // submit an issue/PR as needed

    // Misc -------------------------------------------------------------
                                        ('o'<<16) | ('g'<< 8) | ('x'<< 0),
                                        ('v'<<16) | ('s'<< 8) | ('d'<< 0),
};
static char* mime_types[] = {
    // Web Stuff
    "text/html",
    "text/javascript",
    "text/css",
    "text/javascript",
    "text/html",
    "application/xhtml+xml",

    // Other common text formats
    "application/json",
    "application/ld+json",
    "text/plain",
    "text/csv",
    "application/xml",
    "text/calendar",

    // Binary
    "application/octet-stream",

    // Images
    "image/jpeg",
    "image/jpeg",
    "image/png",
    "image/webp",
    "image/svg+xml",
    "image/gif",
    "image/tiff",
    "image/tiff",
    "image/bmp",
    "image/apng",
    "image/vnd.microsoft.icon",
    "image/avif",

    // Fonts
    "font/oft",
    "font/ttf",
    "font/woff",
    "font/woff2",
    "application/vnd.ms-fontobject",

    // Audio
    "audio/mpeg",
    "audio/wav",
    "audio/opus",
    "audio/ogg",
    "audio/aac",
    "audio/weba",
    "audio/midi",
    "audio/midi",
    "application/x-cdf",

    // Video
    "video/mp4",
    "video/mpeg",
    "video/x-msvideo",
    "video/webm",
    "video/ogg",
    "video/mp2t",

    // Document / Book formats
    "application/pdf",
    "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        // ^^ Somebody's getting paid by the character
    "application/msword",
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
    "application/vnd.ms-excel",
    "application/vnd.openxmlformats-officedocument.presentationml.presentation",
    "application/vnd.ms-powerpoint",
    "application/vnd.oasis.opendocument.text",
    "application/vnd.oasis.opendocument.presentation",
    "application/vnd.oasis.opendocument.spreadsheet",
    "application/epub+zip",
    "application/vnd.amazon.ebook",
    "application/x-abiword",

    // Archive / compression formats
    "application/zip",
    "application/gzip",
    "application/x-tar",
    "application/x-7z-compressed",
    "application/x-bzip",
    "application/x-bzip2",
    "application/x-freearc",

    // Programmer BS
    "application/java-archive",
    "application/x-httpd-php",
    "application/x-sh",
    "application/rtf",
    "application/x-csh",

    // Other programmer extensions are treated as text.
    // TODO: Make this togglable because this is useful but also unexpected
    //       behaviour
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",
    "text/plain",

    // Misc
    "application/ogg",
    "application/vnd.visio",
};

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
        encoding |= c << (i * 8);
    }

    return 0;
}

char* httpsrvdev_file_mime_type(struct httpsrvdev_inst* inst, char* file_path) {
    uint64_t ext_encoding = httpsrvdev_file_encode_ext(inst, file_path);
    if (ext_encoding == 0) {
        if (inst->default_file_mime_type[0] == '\0') {
            inst->err = httpsrvdev_FILE_HAS_NO_EXT;
        }
        return inst->default_file_mime_type;
    }

    // TODO: Do something faster than a linear search
    for (size_t i = 0; i < sizeof(ext_encodings)/sizeof(ext_encodings[0]); ++i) {
        if (ext_encodings[i] == ext_encoding) {
            return mime_types[i];
        }
    }

    inst->err = httpsrvdev_LIB_IMPL_ERR;
    return inst->default_file_mime_type;
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

