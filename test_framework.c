#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// We're keeping everything fixed-sized for convenience
struct test_entry {
    int  depth;
    bool succeeded;
    char description[1024];
    char failure_msg[1024];
};
struct test_entry  test_entries[1024];
size_t             test_entries_count = 0;
struct test_entry* test_stack[16];
size_t             test_stack_depth = 0;

void test_begin(char* description) {
    struct test_entry entry = {
        .depth       = test_stack_depth,
        .succeeded   = true,
        .failure_msg = "",
    };
    strcpy(entry.description, description);

    test_entries[test_entries_count] = entry;
    test_stack[test_stack_depth++] = &test_entries[test_entries_count++];
}

void test_end(void) {
    if (test_stack[--test_stack_depth]->succeeded) return;
    // Otherwise, set `succeeded = false` in ancestors
    for (int i = test_stack_depth; i > -1; --i) {
        test_stack[i]->succeeded = false;
    }
}

bool test_write_results_and_return_true_on_success(int out_fd, int err_fd) {
    bool all_succeeded = true;

    char indent_pattern[1024];
    for (size_t i = 0; i < 1024 - 1; i += 4) {
        stpcpy(indent_pattern + i, "|   ");
    }

    char msg[1024];
    for (size_t i = 0; i < test_entries_count; ++i) {
        struct test_entry entry = test_entries[i];
        int fd;
        char* msg_ptr = msg;
        if (entry.succeeded) {
            fd = out_fd;
              msg_ptr    = stpncpy(msg_ptr, indent_pattern, entry.depth*4);
              msg_ptr    = stpcpy (msg_ptr, "\\---OK: ");
              msg_ptr    = stpcpy (msg_ptr, entry.description);
            *(msg_ptr++) =              '\n';
        } else {
            all_succeeded = false;

            fd = err_fd;
              msg_ptr    = stpncpy(msg_ptr, indent_pattern, entry.depth*4);
              msg_ptr    = stpcpy (msg_ptr, "\\-FAIL: ");
              msg_ptr    = stpcpy (msg_ptr, entry.description);
            *(msg_ptr++) =              '\n';

            if (entry.failure_msg[0] == '\0') goto write_msg;
              msg_ptr    = stpncpy(msg_ptr, indent_pattern, (entry.depth + 1)*4);
              msg_ptr    = stpcpy (msg_ptr, entry.failure_msg);
            *(msg_ptr++) =              '\n';
        }
    write_msg:
        write(fd, msg, msg_ptr - msg);
    }

    return all_succeeded;
}

void test_assert(bool condition, char* fmt, ...) {
    if (condition) return;

    struct test_entry* entry = test_stack[test_stack_depth - 1];
    entry->succeeded = false;

    char* failure_msg_ptr = entry->failure_msg;
    failure_msg_ptr = stpcpy(failure_msg_ptr, "\\-- ");

    va_list  sprintf_args;
    va_start(sprintf_args, fmt);
    vsprintf(failure_msg_ptr, fmt, sprintf_args);
    va_end  (sprintf_args);
}

