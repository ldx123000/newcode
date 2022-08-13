#ifndef _TYPES_H_
#define _TYPES_H_

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

typedef signed char int8;
typedef signed short int16;
typedef signed int int32;
typedef signed long long int64;

typedef int bool;

typedef signed long ssize_t;
typedef unsigned long size_t;

#define NULL ((void *)0)
#define TRUE 1
#define FALSE 0

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define	O_TRUNC		0x400	/* open with truncation */

#endif
