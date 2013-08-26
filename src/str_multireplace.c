/**
 * @file      str_multireplace.c
 * @brief     Multiple key-value replacement in provided string.
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

#include "str_multireplace.h"


