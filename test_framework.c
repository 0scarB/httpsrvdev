#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define test_NODE_GROUP_SUCCESS 0
#define test_NODE_GROUP_FAILED  1
#define test_NODE_ASSERT_FAILED 2
#define test_INDENT_PATTERN "|   |   |   |   |   |   |   |   |   |   |   |   "

// We're keeping everything fixed-sized for convenience
struct test_node {
    int  type;
    int  depth;
    char msg[1024];
};
struct test_node  test_nodes[1024];
size_t            test_nodes_count = 0;
struct test_node* test_stack[16];
size_t            test_stack_depth = 0;
struct test_node* test_added_node;

void test_node_add(int type) {
    test_added_node = &test_nodes[test_nodes_count];
     test_added_node->type  = type;
     test_added_node->depth = test_stack_depth;
    *test_added_node->msg   = '\0';

    test_stack[test_stack_depth] = test_added_node;

    ++test_nodes_count;
}

void test_begin(char* group_description) {
    test_node_add(test_NODE_GROUP_SUCCESS);
    strcpy(test_added_node->msg, group_description);

    ++test_stack_depth;
}

void test_end(void) {
    if (test_stack[test_stack_depth]->type != test_NODE_GROUP_SUCCESS) {
        for (int i = test_stack_depth - 1; i > -1; --i) {
            test_stack[i]->type = test_NODE_GROUP_FAILED;
        }
    }
    --test_stack_depth;
}

bool test_write_results_and_return_true_on_success(int out_fd, int err_fd) {
    bool all_succeeded = true;

    for (size_t i = 0; i < test_nodes_count; ++i) {
        struct test_node node = test_nodes[i];
        int fd;
        char* prefix;
        switch (node.type) {
            case test_NODE_GROUP_SUCCESS:
                fd = out_fd;
                prefix = "\\---OK: ";
                break;
            case test_NODE_GROUP_FAILED:
                fd = err_fd;
                prefix = "\\-FAIL: ";
                all_succeeded = false;
                break;
            case test_NODE_ASSERT_FAILED:
                fd = err_fd;
                prefix = "\\-- ";
                all_succeeded = false;
                break;
        }
        dprintf(fd, "%.*s%s%s\n", node.depth*4, test_INDENT_PATTERN, prefix, node.msg);
    }

    return all_succeeded;
}

void test_assert(bool condition, char* fmt, ...) {
    if (condition) return;

    test_node_add(test_NODE_ASSERT_FAILED);

    va_list  sprintf_args;
    va_start(sprintf_args, fmt);
    vsprintf(test_added_node->msg, fmt, sprintf_args);
    va_end  (sprintf_args);
}

