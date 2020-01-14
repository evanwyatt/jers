extern char error_msg[4096];
extern char sub_test[4096];
extern int __status;
extern int __debug;

extern int total_test_count;
extern int total_failed_count;

#define RED   "\033[1;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"

#define TEST(name, cond) do { \
    snprintf(sub_test, sizeof(sub_test), name); \
    int __test_status = (cond); \
    total_test_count++; \
    printf(" [%s] ", sub_test); \
    fflush(stdout); \
    if (__test_status) { \
        printf(RED "FAILED\n" RESET); \
        __status = 1; \
        total_failed_count++; \
        printf("\tCondition failed: %s\n", #cond); \
    } else { \
        printf(GREEN "Success\n" RESET); \
    } \
} while (0);

#define DEBUG(...) if (__debug) printf(__VA_ARGS__);