#ifndef HTTPSRVDEV_H
#define HTTPSRVDEV_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>

#define httpsrvdev_GET     1
#define httpsrvdev_HEAD    2
#define httpsrvdev_POST    3
#define httpsrvdev_PUT     4
#define httpsrvdev_DELETE  5
#define httpsrvdev_CONNECT 6
#define httpsrvdev_OPTIONS 7
#define httpsrvdev_TRACE   8
#define httpsrvdev_PATCH   9

#define httpsrvdev_NO_ERR                            (int64_t) -1
#define httpsrvdev_CANNOT_PARSE_REQ                  (int64_t) 0x0100000

#define httpsrvdev_FILE_SYS_ERR                      (int64_t) 0x02FF000
#define httpsrvdev_COULD_NOT_OPEN_FILE               (int64_t) 0x0200000
#define httpsrvdev_COULD_NOT_GET_FILE_CONTENT_LENGTH (int64_t) 0x0201000
#define httpsrvdev_COULD_NOT_READ_FILE               (int64_t) 0x0202000
#define httpsrvdev_COULD_NOT_CLOSE_FILE              (int64_t) 0x0204000
#define httpsrvdev_FILE_HAS_NO_EXT                   (int64_t) 0x0208000
#define httpsrvdev_COULD_NOT_OPEN_DIR                (int64_t) 0x0210000
#define httpsrvdev_COULD_NOT_STAT                    (int64_t) 0x0211000
#define httpsrvdev_UNHANDLED_FILE_TYPE               (int64_t) 0x0212000

#define httpsrvdev_INVALID_IP                        (int64_t) 0x0400000
#define httpsrvdev_INVALID_PORT                      (int64_t) 0x0410000

#define httpsrvdev_MEM_ERR                           (int64_t) 0x08FF000
#define httpsrvdev_BUF_TOO_SMALL                     (int64_t) 0x0800000

#define httpsrvdev_LIB_IMPL_ERR                      (int64_t) 0x8000000

#define httpsrvdev_MASK_ERRNO                        (int64_t) 0x0000FFF
#define httpsrvdev_MASK_FILE_TYPE                    (int64_t) 0x0000FFF

struct httpsrvdev_inst {
    int err;

    uint32_t ip;
    int port;
    int listen_sock_fd;
    int   conn_sock_fd;
    struct sockaddr_in listen_sock_addr;
    size_t             listen_sock_addr_size;

    // Request stuff
    char   req_buf[2048];
    size_t req_len;
    int    req_method;
    char*  req_method_str;
    char*  req_target;
    char*  req_headers[128][2];
    int    req_headers_count;
    char*  req_body;

    // Response stuff
    int res_status;

    char* default_file_mime_type;

    char root_path[512];

    /* A region of memory reserved for small, short-lived allocations. */
    char   same_scope_tmp_mem[8192];
    size_t same_scope_tmp_mem_size;
    size_t same_scope_tmp_mem_alloc_offset;
};

struct httpsrvdev_inst httpsrvdev_init_begin();
bool     httpsrvdev_init_end               (struct httpsrvdev_inst* inst);
bool     httpsrvdev_start                  (struct httpsrvdev_inst* inst);
bool     httpsrvdev_stop                   (struct httpsrvdev_inst* inst);
bool     httpsrvdev_res_begin              (struct httpsrvdev_inst* inst);
bool     httpsrvdev_res_send_n             (struct httpsrvdev_inst* inst,
                                                char* str, size_t n);
bool     httpsrvdev_res_send               (struct httpsrvdev_inst* inst, char* str);
bool     httpsrvdev_res_end                (struct httpsrvdev_inst* inst);
bool     httpsrvdev_res_status_line        (struct httpsrvdev_inst* inst, int status);
bool     httpsrvdev_res_header             (struct httpsrvdev_inst* inst,
                                                char* name, char* value);
bool     httpsrvdev_res_headerf            (struct httpsrvdev_inst* inst,
                                                char* name, char* value_fmt, ...);
bool     httpsrvdev_res_body               (struct httpsrvdev_inst* inst, char* body);
bool     httpsrvdev_res_file               (struct httpsrvdev_inst* inst, char* path);
bool     httpsrvdev_res_dir                (struct httpsrvdev_inst* inst, char* path);
bool     httpsrvdev_res_file_sys_entry     (struct httpsrvdev_inst* inst, char* path);
bool     httpsrvdev_res_filef              (struct httpsrvdev_inst* inst, char* file_path_fmt,
                                                ...);
bool     httpsrvdev_res_dirf               (struct httpsrvdev_inst* inst, char* dir_path_fmt,
                                                ...);
bool     httpsrvdev_res_file_sys_entryf    (struct httpsrvdev_inst* inst, char* path_fmt, ...);
bool     httpsrvdev_res_rel_file           (struct httpsrvdev_inst* inst, char* path);
bool     httpsrvdev_res_rel_dir            (struct httpsrvdev_inst* inst, char* path);
bool     httpsrvdev_res_rel_file_sys_entry (struct httpsrvdev_inst* inst, char* path);
bool     httpsrvdev_res_rel_filef          (struct httpsrvdev_inst* inst, char* file_path_fmt,
                                                ...);
bool     httpsrvdev_res_rel_dirf           (struct httpsrvdev_inst* inst, char* dir_path_fmt,
                                                ...);
bool     httpsrvdev_res_rel_file_sys_entryf(struct httpsrvdev_inst* inst, char* path_fmt, ...);
bool     httpsrvdev_res_listing_begin      (struct httpsrvdev_inst* inst);
bool     httpsrvdev_res_listing_entry      (struct httpsrvdev_inst* inst,
                                                char* path, char* link_text);
bool     httpsrvdev_res_listing_end        (struct httpsrvdev_inst* inst);
uint64_t httpsrvdev_file_encode_ext        (struct httpsrvdev_inst* inst, char* file_path);
bool     httpsrvdev_ipv4_from_str          (struct httpsrvdev_inst* inst, char* str);
int64_t  httpsrvdev_ipv4_parse             (struct httpsrvdev_inst* inst, char* str);
bool     httpsrvdev_port_from_str          (struct httpsrvdev_inst* inst, char* str);
int      httpsrvdev_port_parse             (struct httpsrvdev_inst* inst, char* str);

#endif // HTTPSRVDEV_H
