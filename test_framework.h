#include <stdlib.h>

void test_begin(char* description);
void test_end(void);
void test_assert(bool condition, char* fmt, ...);
bool test_write_results_and_return_true_on_success(int out_fd, int err_fd);

