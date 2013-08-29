/**
 * @file      str_multireplace.c
 * @brief     Multiple key-value replacement in provided string.
 * @author    Matej Spanik <mmaster@bitbix.com>
 * @version   0.1
 * @date      2013
 * @copyright Apache License v2
 *
 * This is somewhat complex, but quite fast implementation of multiple pattern
 * string replacement.
 * It can consume some additional memory while processing large strings with
 * large number of replacements. (don't have exact numbers - not tested yet)
 */

#include <stdio.h>
#include "str_multireplace.h"

/**
 * @name String searching
 *
 * This section contains custom Karp-Rabin algorithm implementation optimized
 * for multiple string searching at once.
 */

/** @{ */

#define STR_MR_MATCH_CONTINUE   (0)
#define STR_MR_MATCH_STOP       (1)

/**
 * @brief Match pair internal wrapper structure
 */
typedef struct {
    const str_mr_match_pair *pair; /**< wrapped match pair */
    uint64_t key_hash;             /**< hash of the key */
    uint64_t str_hash;             /**< hash of substring of str */
    uint64_t rem_coef;             /**< char removal coeficient for UNHASH() */
} str_mr_match_pair_wrap;

/**
 * @brief Match callback fuction called when match is found
 *
 * @param[in] str original source string
 * @param[in] where pointer to start of match
 * @param[in] pair pointer to match pair found
 * @return callback returns whether the searching should continue or not
 * @retval STR_MR_MATCH_CONTINUE continue with searching for rest of matches
 * @retval STR_MR_MATCH_STOP stop searching
 */
typedef int (*match_cb)(const char *str, const char *where,
                        const str_mr_match_pair *pair);

/**
 * @brief Compute hash character removal coefficient.
 *
 * Used for first hashed substring character removal in UNHASH() and REHASH()
 *
 * Computes (2^(match_len-1)). Only when match_len < 64 because hash has only
 * 64 bits, anything beyond 64 bits is thrown away automatically.
 *
 * @param[in] match_len length of match string
 * @return removal coefficient used in UNHASH() and REHASH()
 */
#define COMPUTE_REM_COEF(match_len) \
    (match_len < 64 ? (1 << (match_len - 1)) : 0)

/**
 * @brief Hash new character into current hash.
 *
 * Used for getting hash of first substring (str[0..match_len]).
 *
 * @param[in] add_c character that comes to the hash (str[pos])
 * @param[in] cur_hash hash of current matched substring at
 *            str[0..pos-1]
 * @return hash of substring (str[0..pos])
 */
#define HASH(add_c, cur_hash) \
    (((cur_hash) << 1) + (add_c))

/**
 * @brief Remove first character from hashed substring.
 *
 * Used for REHASH for quick computation of substring offsetted by 1.
 *
 * @param[in] rem_c first character from substring that goes away from hash
 *            (str[pos])
 * @param[in] cur_hash hash of current hashed substring
 *            (str[pos..pos+match_len-1])
 * @param[in] rem_coef preprocessed coefficient used in removal of first
 *            character from hash (2^(match_len-1))
 * @return hash of substring with first character removed
 *         (str[pos+1..pos+match_len-1])
 */
#define UNHASH(rem_c, cur_hash, rem_coef) \
    ((cur_hash) - (rem_c) * (rem_coef))

/**
 * @brief Rehash counts hash of next substring of source string.
 *
 * Takes hash of current substring (str[pos..pos+match_len-1]) and counts hash
 * of next substring offsetted by 1 (str[pos+1..pos+match_len]) by removing
 * first character of current substring and adding new character to the end of
 * new substring by using HASH().
 *
 * @param[in] rem_c character that goes away from hash (str[pos])
 * @param[in] add_c character that comes to the hash (str[pos+match_len])
 * @param[in] cur_hash hash of current hashed substring
 *            (str[pos..pos+match_len-1])
 * @param[in] rem_coef preprocessed coefficient used in removal of first
 *            character from hash (2^(match_len-1))
 * @return hash of substring offsetted by 1 (str[pos+1..pos+match_len])
 */
#define REHASH(rem_c, add_c, cur_hash, rem_coef) \
    HASH(add_c, UNHASH(rem_c, cur_hash, rem_coef))

/**
 * @brief String searching using Karp-Rabin algorithm
 *
 * Searches for match in str. Doesn't care about NULL terminators.
 *
 * Note: there are no checks, but function has following assumptions:
 * - str_len > match_len
 * - str != NULL
 * - match != NULL
 *
 * @param[in] str source string
 * @param[in] str_len source string length
 * @param[in] matches wrapped matches array SORTED by length (descending)
 * @param[in] match_cnt count of matches
 * @param[in/out] all_match_cb callback function called for all matches (even
 *                overlapping)
 * @param[in/out] no_overlap_cb callback function called only for
 *                non-overlapping matches
 */
static void
str_mr_kr_search (const char *str, size_t str_len,
                  str_mr_match_pair_wrap *matches, size_t match_cnt,
                  match_cb all_match_cb, match_cb no_overlap_cb)
{
    size_t      i = 1, j = 0;
    size_t      m = 0, first_valid_m = 0;
    uint64_t    str_hash  = 0, last_str_hash = 0;
    size_t      match_len = 0, last_key_len = 0;
    uint64_t    match_hash    = 0, match_hash_start = 0;
    size_t      shortest_match_len = matches[match_cnt - 1].pair->key_length;
    size_t      next_novp_pos = 0; /* next non-overlapping position in string */
    const char *match = NULL;

    if (all_match_cb == NULL && no_overlap_cb == NULL) {
        return;                 /* no reason to live */
    }

    /* count rem_coef for character removal from hash (UNHASH()/REHASH()) */
    for (m = 0; m < match_cnt; m++) {
        matches[m].rem_coef = COMPUTE_REM_COEF(matches[m].pair->key_length);
    }

    /* count hash of first match_len characters of both match and str
     * strings for each match */
    for (m = 0; m < match_cnt; m++) {
        matches[m].key_hash = matches[m].str_hash = 0;
        match_len = matches[m].pair->key_length;

        /* check if we didn't already count str_hash for this */
        if (match_len == last_key_len) {
            matches[m].str_hash = last_str_hash;

            /* hash only last 64 characters */
            match_hash_start = (match_len > 64 ? match_len - 64 : 0);

            /* cool now we only need to copute key hash */
            for (i = match_hash_start; i < match_len; i++) {
                matches[m].key_hash = HASH(matches[m].pair->key[i],
                                           matches[m].key_hash);
            }

            continue;
        }

        for (i = 0; i < match_len; i++) {
            matches[m].key_hash = HASH(matches[m].pair->key[i],
                                       matches[m].key_hash);
            matches[m].str_hash = HASH(str[i], matches[m].str_hash);
        }

        /* remember last match key length and str_hash */
        last_key_len  = match_len;
        last_str_hash = matches[m].str_hash;
    }

    /* walk through the source string and try to find a match */
    while (j < str_len - shortest_match_len) {
        /* reset stored variables */
        last_key_len  = 0;
        last_str_hash = 0;

        /* walk all matches that can fit into the source string at j */
        for (m = first_valid_m; m < match_cnt; m++) {
            match_len = matches[m].pair->key_length;
            /* if a match cannot fit, skip it next time */
            if (j >= str_len - match_len) {
                first_valid_m = m + 1;
                continue;
            }

            str_hash   = matches[m].str_hash;
            match      = matches[m].pair->key;
            match_hash = matches[m].key_hash;

            /* if it's the same match_len as last key */
            if (match_len == last_key_len) {
                /* use the last computed hash */
                matches[m].str_hash = last_str_hash;
                /* str_hash stays the same as last time */
            } else {
                /* compute hash of next substring */
                matches[m].str_hash =
                    REHASH(str[j], str[j + match_len], str_hash,
                           matches[m].rem_coef);
            }

            /*
             * compare hashes and memory (if hashes are equal)
             * go in only if all matches callback is set or j is behind
             * end of last match
             */
            if (((all_match_cb != NULL) || (j >= next_novp_pos)) &&
                (match_hash == str_hash) &&
                (memcmp(match, str + j, match_len) == 0)) {
                /*
                 * match found starting at str[j] (including)
                 */
                if (all_match_cb != NULL) {
                    all_match_cb(str, str + j, matches[m].pair);
                }

                if (no_overlap_cb != NULL) {
                    if (j >= next_novp_pos) {
                        no_overlap_cb(str, str + j, matches[m].pair);
                        next_novp_pos = j + match_len;
                    }
                }

                /* temporary solution for debugging */
                printf("Found match for '%s' at position %d.\n", match, j);
            }

            /*
             * store hash for next match (if it has same length)
             */
            last_str_hash = matches[m].str_hash;
            last_key_len  = match_len;
        }

        j++;
    }
}

/** @} */
