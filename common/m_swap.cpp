// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id: m_swap.cpp 3174 2012-05-11 01:03:43Z mike $
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2006-2012 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Endianess handling, swapping 16bit and 32bit.
//
//-----------------------------------------------------------------------------

// ONLY for msvc! these make gcc debug builds GARGANTUAN
// eg: 30mb compared to 13mb!
#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#ifndef _XBOX
#include <windows.h>
#endif // !_XBOX
#endif // MSC_VER

#include "version.h"
#include "m_swap.h"

// Endianess handling.
// WAD files are stored little endian.
#ifdef __BIG_ENDIAN__

// Swap 16bit, that is, MSB and LSB byte.
// No masking with 0xFF should be necessary. 
short SHORT (short x)
{
	return (short)((((unsigned short)x)>>8) | (((unsigned short)x)<<8));
}

unsigned short SHORT (unsigned short x)
{
	return (unsigned short)((x>>8) | (x<<8));
}

// Swapping 32bit.
unsigned int LONG (unsigned int x)
{
	return (unsigned int)(
		(x>>24)
		| ((x>>8) & 0xff00)
		| ((x<<8) & 0xff0000)
		| (x<<24));
}

int LONG (int x)
{
	return (int)(
		(((unsigned int)x)>>24)
		| ((((unsigned int)x)>>8) & 0xff00)
		| ((((unsigned int)x)<<8) & 0xff0000)
		| (((unsigned int)x)<<24));
}

#else

short BESHORT (short x)
{
	return (short)((((unsigned short)x)>>8) | (((unsigned short)x)<<8));
}

unsigned short BESHORT (unsigned short x)
{
	return (unsigned short)((x>>8) | (x<<8));
}

unsigned int BELONG (unsigned int x)
{
	return (unsigned int)(
		(x>>24)
		| ((x>>8) & 0xff00)
		| ((x<<8) & 0xff0000)
		| (x<<24));
}

int BELONG (int x)
{
	return (int)(
		(((unsigned int)x)>>24)
		| ((((unsigned int)x)>>8) & 0xff00)
		| ((((unsigned int)x)<<8) & 0xff0000)
		| (((unsigned int)x)<<24));
}

#endif // __BIG_ENDIAN__

VERSION_CONTROL (m_swap_cpp, "$Id: m_swap.cpp 3174 2012-05-11 01:03:43Z mike $")