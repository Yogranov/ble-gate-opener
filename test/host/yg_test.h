/* Minimal test helpers for the logic that runs on a PC. */
#pragma once

#include <stdio.h>
#include <string.h>

static int yg_test_failures;
static int yg_test_checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        yg_test_checks++;                                               \
        if (!(cond)) {                                                  \
            yg_test_failures++;                                         \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                               \
    } while (0)

#define CHECK_INT(actual, expected)                                     \
    do {                                                                \
        long long a_ = (long long)(actual);                             \
        long long e_ = (long long)(expected);                           \
        yg_test_checks++;                                               \
        if (a_ != e_) {                                                 \
            yg_test_failures++;                                         \
            printf("  FAIL %s:%d: %s = %lld, expected %lld\n",          \
                   __FILE__, __LINE__, #actual, a_, e_);                \
        }                                                               \
    } while (0)

#define CHECK_STR(actual, expected)                                     \
    do {                                                                \
        const char *a_ = (actual);                                      \
        const char *e_ = (expected);                                    \
        yg_test_checks++;                                               \
        if (strcmp(a_, e_) != 0) {                                      \
            yg_test_failures++;                                         \
            printf("  FAIL %s:%d: %s = \"%s\", expected \"%s\"\n",      \
                   __FILE__, __LINE__, #actual, a_, e_);                \
        }                                                               \
    } while (0)

#define CHECK_HAS(haystack, needle)                                     \
    do {                                                                \
        const char *h_ = (haystack);                                    \
        const char *n_ = (needle);                                      \
        yg_test_checks++;                                               \
        if (strstr(h_, n_) == NULL) {                                   \
            yg_test_failures++;                                         \
            printf("  FAIL %s:%d: \"%s\" not found in \"%s\"\n",        \
                   __FILE__, __LINE__, n_, h_);                         \
        }                                                               \
    } while (0)

#define CHECK_LACKS(haystack, needle)                                   \
    do {                                                                \
        const char *h_ = (haystack);                                    \
        const char *n_ = (needle);                                      \
        yg_test_checks++;                                               \
        if (strstr(h_, n_) != NULL) {                                   \
            yg_test_failures++;                                         \
            printf("  FAIL %s:%d: \"%s\" unexpectedly in \"%s\"\n",     \
                   __FILE__, __LINE__, n_, h_);                         \
        }                                                               \
    } while (0)

#define RUN(test)                                                       \
    do {                                                                \
        int before_ = yg_test_failures;                                 \
        test();                                                         \
        printf("%s %s\n", yg_test_failures == before_ ? "ok  " : "FAIL", #test); \
    } while (0)

#define TEST_RESULT()                                                   \
    (printf("%d checks, %d failures\n", yg_test_checks, yg_test_failures), \
     yg_test_failures ? 1 : 0)
