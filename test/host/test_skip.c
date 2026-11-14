/* test_skip.c — stands in for a test whose tool is missing, so CTest
 * reports it skipped rather than leaving it out (design.md §15.1). The
 * reason is given at build time. */

#include <stdio.h>

#include "test_util.h"

int main(void) {
    printf("skipped: %s\n", SKIP_REASON);
    return TEST_SKIP_CODE;
}
