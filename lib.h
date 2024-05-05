#include <netinet/in.h>

struct httpsrvdev_inst {
    int ip;
    int port;
    int listen_sock_fd;
    int   conn_sock_fd;
    struct sockaddr_in listen_sock_addr;
    size_t             listen_sock_addr_size;
};

struct httpsrvdev_inst httpsrvdev_init_begin();
int httpsrvdev_init_end (struct httpsrvdev_inst* inst);
int httpsrvdev_start    (struct httpsrvdev_inst* inst);
int httpsrvdev_res_begin(struct httpsrvdev_inst* inst);
int httpsrvdev_res_end  (struct httpsrvdev_inst* inst);
int httpsrvdev_stop     (struct httpsrvdev_inst* inst);

