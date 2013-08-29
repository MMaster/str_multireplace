/**
 * @file      test.c
 * @brief     Basic test of multiple key-value replacement in provided string.
 * @author    Matej Spanik <mmaster@bitbix.com>
 * @version   0.1
 * @date      2013
 * @copyright Apache License v2
 *
 * Very lame test.
 *
 * Compile with:
 *    $ gcc -o test test.c str_replace.c
 */
#include <stdio.h>
#include <string.h>
#include "str_multireplace.h"

int
main ()
{
    str_mr_match_pair mps[] = {
        {"1",  1, "One", 3},         {"2",     1, "Two",  3},
        {"33", 2, "Threethree", 10}, {"abcde", 5, "A..e", 4},
    };
    size_t mp_cnt = sizeof(mps) / sizeof(str_mr_match_pair);
    const char *str   = "1233abcde2331122233333abcdeabcdeaaabcdefg";
    size_t str_len    = strlen(str);
    char  *result     = NULL;
    size_t result_len = 0;

    printf("str: %s\n", str);
    str_multireplace(str, str_len, mps, mp_cnt, &result, &result_len, true);
    printf("result: %s\n", result);
    return 0;
}

