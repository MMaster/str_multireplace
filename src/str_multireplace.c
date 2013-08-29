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

#include <string.h>
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
typedef int (*str_mr_match_cb)(const char *str, const char *where,
                               const str_mr_match_pair *pair, void *cb_ctx);

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
 * - str != NULL
 * - matches != NULL
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
                  str_mr_match_cb all_match_cb, str_mr_match_cb no_overlap_cb,
                  void *cb_ctx)
{
    size_t      i = 1, j = 0;
    size_t      m = 0, first_valid_m = 0;
    uint64_t    str_hash   = 0, last_str_hash = 0;
    size_t      match_len  = 0, last_key_len = 0;
    uint64_t    match_hash = 0, match_hash_start = 0;
    size_t      shortest_match_len = matches[match_cnt - 1].pair->key_length;
    size_t      next_novp_pos = 0; /* next non-overlapping position in string */
    const char *match = NULL;
    int status = STR_MR_MATCH_CONTINUE;

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
        if (match_len > str_len) {
            first_valid_m = m + 1;
            continue;
        }

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
             * go in only if all match callback is set or j is behind
             * end of last match
             */
            if (((all_match_cb != NULL) || (j >= next_novp_pos)) &&
                (match_hash == str_hash) &&
                (memcmp(match, str + j, match_len) == 0)) {
                /*
                 * match found starting at str[j] (including)
                 */
                if (all_match_cb != NULL) {
                    status =
                        all_match_cb(str, str + j, matches[m].pair, cb_ctx);
                }

                if (j >= next_novp_pos) {
                    if (no_overlap_cb != NULL) {
                        status = no_overlap_cb(str, str + j, matches[m].pair,
                                               cb_ctx);
                    }

                    next_novp_pos = j + match_len;
                }

                if (status == STR_MR_MATCH_STOP) {
                    break;
                }
            }

            /*
             * store hash for next match (if it has same length)
             */
            last_str_hash = matches[m].str_hash;
            last_key_len  = match_len;
        }

        if (status == STR_MR_MATCH_STOP) {
            break;
        }

        j++;
    }
}

/** @} */

/**
 * @name String replacement
 *
 * This section contains string replacement functionality
 */
/** @{ */

#define STR_MR_PREALLOC_OCCURENCES      (32)
#define STR_MR_MAX_QUEUE_GROW           (1024)

#define MIN(x, y) (x > y ? y : x)

/**
 * @brief Matched pair pointer
 */
typedef struct {
    size_t pos;                    /* position in source buffer */
    const str_mr_match_pair *pair; /* match pair found */
} str_mr_matched_pair;

/**
 * @brief Matched pairs queue
 */
typedef struct {
    str_mr_matched_pair *mps;   /* matched pairs array */
    size_t mp_cnt;              /* number of matched pairs in array */
    size_t mp_alloc_cnt;        /* number of matched pairs alloc'ed */
    size_t offset;              /* offset after last replacement */
} str_mr_mp_queue;

/**
 * @brief Initialize matched pairs queue
 */
static
str_mr_mp_queue *
str_mr_mp_queue_init (size_t prealloc_cnt)
{
    str_mr_mp_queue *mpq = (str_mr_mp_queue *)malloc(sizeof(str_mr_mp_queue));

    if (mpq == NULL) {
        return NULL;
    }

    mpq->mps = (str_mr_matched_pair *)calloc(STR_MR_PREALLOC_OCCURENCES,
                                             sizeof(str_mr_matched_pair));
    if (mpq->mps == NULL) {
        free(mpq);
        return NULL;
    }

    mpq->mp_alloc_cnt = STR_MR_PREALLOC_OCCURENCES;

    return mpq;
}

/**
 * @brief Free matched pairs queue
 */
static
void
str_mr_mp_queue_free (str_mr_mp_queue *mpq)
{
    free(mpq->mps);
    free(mpq);
}

/**
 * @brief Grow matched pairs queue as needed
 *
 * Grows queue (max. by STR_MR_MAX_QUEUE_GROW)
 *
 * @return status code
 * @retval STR_MR_ERROR_SUCCESS successfuly grown
 * @retval STR_MR_ERROR_OOM out of memory
 * @retval STR_MR_ERROR_INVALID_ARG invalid argument provided
 */
static
int32_t
str_mr_mp_queue_grow (str_mr_mp_queue *mpq)
{
    str_mr_matched_pair *new_mps = NULL;
    size_t new_mp_alloc_cnt = 0;
    size_t grow_by = 0;

    if (mpq == NULL) {
        return STR_MR_ERROR_INVALID_ARG;
    }

    grow_by = MIN(mpq->mp_alloc_cnt, STR_MR_MAX_QUEUE_GROW);
    new_mp_alloc_cnt = mpq->mp_alloc_cnt + grow_by;

    new_mps = (str_mr_matched_pair *)realloc(mpq->mps,
                                             new_mp_alloc_cnt *
                                             sizeof(str_mr_matched_pair));
    if (new_mps == NULL) {
        return STR_MR_ERROR_OOM;
    }

    mpq->mps = new_mps;
    mpq->mp_alloc_cnt = new_mp_alloc_cnt;

    return STR_MR_ERROR_SUCCESS;
}

/**
 * @brief Add new matched pair to queue
 *
 * @return status code
 * @retval STR_MR_ERROR_SUCCESS successfuly grown
 * @retval STR_MR_ERROR_OOM out of memory
 * @retval STR_MR_ERROR_INVALID_ARG invalid argument provided
 */
static
int32_t
str_mr_mp_queue_add (str_mr_mp_queue *mpq, size_t pos,
                     const str_mr_match_pair *pair)
{
    int32_t rc = STR_MR_ERROR_SUCCESS;
    str_mr_matched_pair *mp = NULL;

    if (mpq == NULL || pair == NULL) {
        return STR_MR_ERROR_INVALID_ARG;
    }

    /* grow memory if needed */
    if (mpq->mp_cnt >= mpq->mp_alloc_cnt) {
        rc = str_mr_mp_queue_grow(mpq);
        if (rc != STR_MR_ERROR_SUCCESS) {
            return rc;
        }
    }

    /* set new matched pair in queue */
    mp = &(mpq->mps[mpq->mp_cnt]);
    mp->pos  = pos;
    mp->pair = pair;

    /* increase queue counter */
    mpq->mp_cnt++;

    /* move offset */
    mpq->offset += pair->value_length - pair->key_length;
    return rc;
}

/**
 * @brief Callback when match is found
 *
 * @return callback returns whether the searching should continue or not
 * @retval STR_MR_MATCH_CONTINUE continue with searching for rest of matches
 * @retval STR_MR_MATCH_STOP stop searching
 */
int
str_mr_match_callback (const char *str, const char *where,
                       const str_mr_match_pair *pair, void *ctx)
{
    str_mr_mp_queue *mpq = (str_mr_mp_queue *)ctx;

    /* add to queue */
    str_mr_mp_queue_add(mpq, (where - str), pair);

    return STR_MR_MATCH_CONTINUE;
}

/**
 * @brief qsort compare function
 */
int
str_mr_mp_compare (const void *x0, const void *x1)
{
    str_mr_match_pair_wrap *p0 = (str_mr_match_pair_wrap *)x0;
    str_mr_match_pair_wrap *p1 = (str_mr_match_pair_wrap *)x1;

    if (p0->pair->key_length == p1->pair->key_length) {
        return 0;
    }

    if (p0->pair->key_length > p1->pair->key_length) {
        return -1;
    }

    return 1;
}

/**
 * @brief Function to replace all occurrences of match pairs in buffer.
 *
 * @see str_multireplace.h
 */
int32_t
str_multireplace (const char *str, size_t str_len,
                  const str_mr_match_pair *match_pairs, size_t match_pair_cnt,
                  char **result, size_t *result_len, bool terminate)
{
    int i = 0;
    int32_t rc    = 0;
    char   *r     = NULL;
    size_t  r_len = 0;
    size_t  alloc_len = 0;
    size_t  str_pos   = 0;
    size_t  offset    = 0;

    str_mr_mp_queue *mpq = NULL;               /* matched pairs queue */
    str_mr_match_pair_wrap *sorted_mps = NULL; /* sorted wrapped match pairs */
    str_mr_matched_pair    *mp = NULL;         /* match pair helper pointer */

    if ((str == NULL) || (str_len <= 0) || (match_pairs == NULL) ||
        (match_pair_cnt <= 0) || (result == NULL) || (result_len == NULL)) {
        return STR_MR_ERROR_INVALID_ARG;
    }

    mpq = str_mr_mp_queue_init(STR_MR_PREALLOC_OCCURENCES);
    if (mpq == NULL) {
        return STR_MR_ERROR_OOM;
    }

    sorted_mps = (str_mr_match_pair_wrap *)calloc(match_pair_cnt,
                                                  sizeof(str_mr_match_pair_wrap));
    if (sorted_mps == NULL) {
        str_mr_mp_queue_free(mpq);
        return STR_MR_ERROR_OOM;
    }

    for (i = 0; i < match_pair_cnt; i++) {
        sorted_mps[i].pair = &match_pairs[i];
    }

    qsort(sorted_mps, match_pair_cnt, sizeof(str_mr_match_pair_wrap),
          str_mr_mp_compare);

    str_mr_kr_search(str, str_len, sorted_mps, match_pair_cnt,
                     NULL, str_mr_match_callback, mpq);

    if (mpq->mp_cnt <= 0) {
        alloc_len = str_len;
        if (terminate == true) {
            alloc_len++;
        }

        *result = (char *)malloc(alloc_len * sizeof(char));
        if (*result == NULL) {
            rc = STR_MR_ERROR_OOM;
        } else {
            memcpy(*result, str, str_len * sizeof(char));
            if (terminate == true) {
                *result[str_len] = '\0';
            }

            *result_len = str_len;
        }
    } else {
        rc    = mpq->mp_cnt;
        r_len = str_len + mpq->offset;
        alloc_len = r_len;
        if (terminate) {
            alloc_len++;
        }

        r = (char *)malloc(alloc_len * sizeof(char));
        if (r == NULL) {
            rc = STR_MR_ERROR_OOM;
        }

        for (i = 0; i < mpq->mp_cnt; i++) {
            mp = &mpq->mps[i];

            memcpy(r + str_pos + offset, str + str_pos, mp->pos - str_pos);
            str_pos = mp->pos + mp->pair->key_length;
            memcpy(r + mp->pos + offset, mp->pair->value,
                   mp->pair->value_length);
            offset += mp->pair->value_length - mp->pair->key_length;
        }

        memcpy(r + str_pos + offset, str + str_pos, str_len - str_pos);
        if (terminate) {
            r[r_len] = '\0';
        }

        *result = r;
        *result_len = r_len;
    }

    /* cleanup */
    free(sorted_mps);
    str_mr_mp_queue_free(mpq);

    return rc;
}

/** @} */

