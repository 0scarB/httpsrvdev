#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "lib.h"

struct httpsrvdev_inst httpsrvdev_init_begin() {
    struct httpsrvdev_inst inst = {
        .err = httpsrvdev_NO_ERR,

        .ip   = (127<<24) | (0<<16) | (0<<8) | (1<<0),
        .port = 8080,
        .listen_sock_fd = -1,
        .  conn_sock_fd = -1,

        .res_chunk_len = 0,
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
    printf("Listening...\n");

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
    inst->req_target_slice[1] = i;

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

    inst->req_len = read(inst->conn_sock_fd, inst->req_buf, 2048);
    if (!parse_req(inst)) {
        return false;
    }

    return true;
}

bool httpsrvdev_res_end(struct httpsrvdev_inst* inst) {
    write(inst->conn_sock_fd, inst->res_chunk_buf, 2048);
    close(inst->conn_sock_fd);
    inst->conn_sock_fd = -1;

    return true;
}

bool httpsrvdev_stop(struct httpsrvdev_inst* inst) {
    if (inst->conn_sock_fd != -1) {
        close(inst->conn_sock_fd);
    }
    close(inst->listen_sock_fd);

    return true;
}

char* httpsrvdev_req_slice_start(struct httpsrvdev_inst* inst, uint16_t(*slice)[2]) {
    return inst->req_buf + ((size_t) (*slice)[0]);
}

size_t httpsrvdev_req_slice_len(struct httpsrvdev_inst* inst, uint16_t(*slice)[2]) {
    return (*slice)[1] - (*slice)[0];
}

char* httpsrvdev_req_slice(struct httpsrvdev_inst* inst, uint16_t(*slice)[2]) {
    size_t len = httpsrvdev_req_slice_len(inst, slice);
    char* ptr = malloc(len + 1);
    strncpy(ptr, httpsrvdev_req_slice_start(inst, slice), len);

    return ptr;
}

bool httpsrvdev_res_chunk_write_n(struct httpsrvdev_inst* inst, char* str, size_t n) {
    size_t res_chunk_len_after_write = inst->res_chunk_len + n;
    if (res_chunk_len_after_write > 2048) {
        inst->err = httpsrvdev_EXCEEDED_MAX_RES_CHUNK_SIZE;
        return false;
    }

    strncpy(inst->res_chunk_buf + inst->res_chunk_len, str, n);
    inst->res_chunk_len = res_chunk_len_after_write;

    return true;
}

bool httpsrvdev_res_chunk_send(struct httpsrvdev_inst* inst) {
    write(inst->conn_sock_fd, inst->res_chunk_buf, inst->res_chunk_len);
    inst->res_chunk_len = 0;

    return true;
}

bool httpsrvdev_res_send_n(struct httpsrvdev_inst* inst, char* str, size_t n) {
    while (n > 2048) {
        if (!httpsrvdev_res_chunk_write_n(inst, str, 2048)) return false;
        if (!httpsrvdev_res_chunk_send(inst))               return false;
        str += 2048;
        n   -= 2048;
    }
    if (n > 0) {
        if (!httpsrvdev_res_chunk_write_n(inst, str, n))    return false;
        if (!httpsrvdev_res_chunk_send(inst))               return false;
    }

    return true;
}

bool httpsrvdev_res_send(struct httpsrvdev_inst* inst, char* str) {
    return httpsrvdev_res_send_n(inst, str, strlen(str));
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
    if (!httpsrvdev_res_send(inst, name))   return false;
    if (!httpsrvdev_res_send(inst, ": "))   return false;
    if (!httpsrvdev_res_send(inst, value))  return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;

    return true;
}

bool httpsrvdev_res_body(struct httpsrvdev_inst* inst, char* body) {
    char content_len_value_buf[20];
    sprintf(content_len_value_buf, "%ld", strlen(body));
    if (!httpsrvdev_res_header(inst, "Content-Length", content_len_value_buf))
        return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;
    if (!httpsrvdev_res_send(inst, body))   return false;
    if (!httpsrvdev_res_send(inst, "\r\n")) return false;

    return true;
}

