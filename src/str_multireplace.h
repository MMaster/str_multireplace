
/**
 * @file      str_multireplace.h
 * @brief     Header for multiple key-value replacement in provided string.
 * @author    Matej Spanik
 * @version   0.1
 * @date      2013
 * @copyright Apache License v2
 * 
 * This is somewhat complex, but quite fast implementation of multiple pattern
 * string replacement.
 * It can consume some additional memory while processing large strings with 
 * large number of replacements. (don't have exact numbers - not tested yet)
 */

#ifndef __STR_MULTIREPLACE_H__
#define __STR_MULTIREPLACE_H__

/**
 * Out of memory error
 */
#define STR_MR_ERROR_OOM            (-1)

/**
 * Invalid argument provided error 
 * (usually means something was NULL where it shouldn't be)
 */
#define STR_MR_ERROR_INVALID_ARG    (-2)

/**
 * Invalid match pair provided
 * (usually means key or value in some match pair is NULL)
 */
#define STR_MR_ERROR_INVALID_MATCH  (-3)

/**
 * @brief Match key-value string pair
 */
typedef struct {
    const char *key;            /**< key that should be replaced */
    size_t      key_length;     /**< length of the key (w/o NULL termin.) */
    const char *value;          /**< value put in place of key */
    size_t      value_length;   /**< length of the value (w/o NULL termin.) */
} str_mr_match_pair;

/**
 * @brief Function to replace all occurrences of match pairs in string str.
 *
 * Replaces all occurrences of key from provided match_pairs array with the
 * value associated with that key in specified string.
 * Doesn't care about NULL terminators so you can use it on any buffer.
 *
 * Note: Caller is responsible for freeing the result.
 *
 * @param[in] str source string
 * @param[in] str_len source string length (without NULL terminator)
 * @param[in] match_pairs match pairs array
 * @param[in] match_pair_cnt number of match pairs in match_pair array
 * @param[out] result newly allocated NULL terminated string containing all 
 *             replacements
 * @param[out] result_len length of result string
 *
 * @return number of replacements made or negative number on error
 * @retval STR_MR_ERROR_OOM out of memory
 * @retval STR_MR_ERROR_INVALID_ARG invalid argument provided
 * @retval STR_MR_ERROR_INVALID_MATCH invalid match pair provided
 */
int32_t
str_multireplace(const char *str, size_t str_len,
                 const str_mr_match_pair *match_pairs, size_t match_pair_cnt,
                 char **result, size_t *result_len);

#endif
