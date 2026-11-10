/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Unit Local Header File
 *
 *	This file defines private structures, variables, and routines
 *	required for the TRIGGER library functions.
 ******************************************************************************/
#ifndef	TRIGLOCAL_H
#define	TRIGLOCAL_H

#if	defined( __cplusplus )
extern	"C"	{
#endif

#include	<dirent.h>
#include	<errno.h>
#include	<fcntl.h>
#include	<inttypes.h>
#include	<poll.h>
#include	<signal.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/mman.h>
#include	<sys/prctl.h>
#include	<sys/stat.h>
#include	<sys/time.h>
#include	<sys/types.h>
#include	<sys/wait.h>
#include	<time.h>
#include	<unistd.h>

#include	<trigger.h>

/*
 *	Shared Word Access: the atomic unit (lock free xchg, gcc and c-saw alike)
 */

#include	<atomic_bump.h>

#define	TRIG_STORE( p, v )	Atomic_Store( ( p ), (u32)( v ) )
#define	TRIG_XCHG( p, v )	Atomic_Exchange( ( p ), (u32)( v ) )

#define	TRIG_MAX_PEND	64		/* Debounce Queue Slots */
#define	TRIG_LONG_PATH	( TRIG_PATH_LEN * 4 )

typedef	struct	trig_root_s	{
	char	path[ TRIG_PATH_LEN ];
	u32	events;			/* Caller's Event Mask */
	u32	flags;			/* TRIG_F_* */
	u32	debounce_ms;
	s32	tag;
	s32	wd;			/* Root Watch, -1 When Gone */
	u32	isdir;
}	trig_root_t;

typedef	struct	trig_wd_s	{
	s32	wd;
	s32	root;			/* Index In roots[ ] */
	char	path[ TRIG_LONG_PATH ];
}	trig_wd_t;

typedef	struct	trig_pend_s	{
	s32	root;
	u32	mask;
	u64	due;			/* Trig_Mono( ) msec */
	char	path[ TRIG_LONG_PATH ];
}	trig_pend_t;

struct	trig_w_s	{
	int		fd;
	int		nroot;
	int		maxroot;
	trig_root_t *	roots;
	int		nwd;
	int		maxwd;
	trig_wd_t *	wds;
	int		npend;
	u64		retry;		/* Next Gone Root Retry (msec) */
	trig_pend_t	pend[ TRIG_MAX_PEND ];
};

#if	defined( __cplusplus )
}
#endif

#endif
