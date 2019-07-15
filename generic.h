/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_GENERIC_HEADER
#define FEMC_DRIVER_GENERIC_HEADER

// attributes

#ifdef __GNUC__
#define UNUSED(VAR)  VAR __attribute__((unused))
#define WEAK_LINKAGE __attribute__((weak))
#endif

// preprosessor magic

#define stringify(X)  stringify_(X)
#define stringify_(X) #X

#define concat(a,b)   concat_(a,b)
#define concat_(a,b)  a##b

// printf integer length modifiers

#if __SIZEOF_LONG__ == 8
#define PRINTF_INT64_MOD "l"
#elif __SIZEOF_LONG__ == 4 && __SIZEOF_LONG_LONG__ == 8
#define PRINTF_INT64_MOD "ll"
#endif

// debug prints

#ifdef FD_DEBUG
# include <stdio.h>
# define MARKER         printf("AT " __FILE__ ":%u (%s)\n", __LINE__, __func__)
# define MARKER1(X)     printf("AT #" X "\n")
# define MARKER2(X1,X2) printf("AT #" X1 " " X2 "\n")
#else
# define MARKER         do {} while(0)
# define MARKER1(X)     do {} while(0)
# define MARKER2(X1,X2) do {} while(0)
#endif

#endif
