#include <netinet/in.h>
#include <stdbool.h>

#define httpsrvdev_GET     1
#define httpsrvdev_HEAD    2
#define httpsrvdev_POST    3
#define httpsrvdev_PUT     4
#define httpsrvdev_DELETE  5
#define httpsrvdev_CONNECT 6
#define httpsrvdev_OPTIONS 7
#define httpsrvdev_TRACE   8
#define httpsrvdev_PATCH   9

#define httpsrvdev_NO_ERR                      -1
#define httpsrvdev_CANNOT_PARSE_REQ             1
#define httpsrvdev_EXCEEDED_MAX_RES_CHUNK_SIZE  2

struct httpsrvdev_inst {
    int err;

    int ip;
    int port;
    int listen_sock_fd;
    int   conn_sock_fd;
    struct sockaddr_in listen_sock_addr;
    size_t             listen_sock_addr_size;

    // Request stuff
    char     req_buf[2048];
    size_t   req_len;
    int      req_method;
    uint16_t req_target_slice         [2];
    uint16_t req_header_slices[128][2][2];
    int      req_headers_count;
    uint16_t req_body_slice           [2];

    // Response stuff
    char   res_chunk_buf[2048];
    size_t res_chunk_len;
};

struct httpsrvdev_inst httpsrvdev_init_begin();
bool   httpsrvdev_init_end         (struct httpsrvdev_inst* inst);
bool   httpsrvdev_start            (struct httpsrvdev_inst* inst);
bool   httpsrvdev_stop             (struct httpsrvdev_inst* inst);
char*  httpsrvdev_req_slice_start  (struct httpsrvdev_inst* inst, uint16_t(*slice)[2]);
size_t httpsrvdev_req_slice_len    (struct httpsrvdev_inst* inst, uint16_t(*slice)[2]);
char*  httpsrvdev_req_slice        (struct httpsrvdev_inst* inst, uint16_t(*slice)[2]);
bool   httpsrvdev_res_begin        (struct httpsrvdev_inst* inst);
bool   httpsrvdev_res_end          (struct httpsrvdev_inst* inst);
bool   httpsrvdev_res_chunk_write_n(struct httpsrvdev_inst* inst, char* str, size_t n);
bool   httpsrvdev_res_chunk_send   (struct httpsrvdev_inst* inst);
bool   httpsrvdev_res_send_n       (struct httpsrvdev_inst* inst, char* str, size_t n);
bool   httpsrvdev_res_send         (struct httpsrvdev_inst* inst, char* str);
bool   httpsrvdev_res_status_line  (struct httpsrvdev_inst* inst, int status);
bool   httpsrvdev_res_header       (struct httpsrvdev_inst* inst, char* name, char* value);
bool   httpsrvdev_res_body         (struct httpsrvdev_inst* inst, char* body);

