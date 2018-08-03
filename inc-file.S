/* Femc Driver
 * Copyright (C) 2011-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 *
 * -----
 *
 * Defines
 * -------
 * FileName
 * SymbolName
 * NoLenSymbol (optional)
 * LenSymbolName (optional, default: SymbolName##_len)
 *
 */

#include "generic.h"

#if __SIZEOF_POINTER__ == 8
# define PTRSIZE .quad
#elif __SIZEOF_POINTER__ == 4
# define PTRSIZE .long
#else
# error PTRSIZE is matched with 'unsigned long' in C/C++.
# error I suppose it should be equal to the size of pointers.
#endif

	.text
	.globl SymbolName
SymbolName:
	.incbin stringify(FileName)

#ifdef NoLenSymbol

	.byte 0

#else
# ifndef LenSymbolName
# define LenSymbolName concat(SymbolName,_len)
# endif

	.globl LenSymbolName
LenSymbolName:
	PTRSIZE . - SymbolName

#endif
