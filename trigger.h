/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Unit Header File
 *
 *	This file defines the .trigmem shared memory file layout, the
 *	trigger event and control definitions, and the TRIGGER library
 *	functions used to watch files and directory trees (inotify).
 ******************************************************************************/

#ifndef	TRIGGER_H
#define	TRIGGER_H

#if	defined( __cplusplus )
extern	"C"	{
#endif

#include	<sys/types.h>
#include	<sys/inotify.h>
#include	<common.h>

/*
 *	Trace Levels
 *
 *	TRIG_TRC_LVL is the global (upper 16 bit) flag for the shared
 *	TRIGGER library functions.  The lower 16 bits belong to the
 *	unit's own programs (trigger server, trigmem tool).
 */

#define	TRIG_TRC_LVL	0x20000000	/* TRIGGER Unit's Trace Level */

#define	TRIG_T_WATCH	0x00000001	/* trigger: watch setup / removal */
#define	TRIG_T_EVENT	0x00000002	/* trigger: each event delivered */
#define	TRIG_T_SPAWN	0x00000004	/* trigger: each fork & exec */
#define	TRIG_T_CHILD	0x00000008	/* trigger: each child exit */
#define	TRIG_T_CTL	0x00000010	/* trigger: control word & reloads */
#define	TRIG_T_LOOP	0x00000020	/* trigger: main loop heartbeat */
#define	TRIG_T_TOOL	0x00000100	/* trigmem: tool detail */

/*
 *	Sizes & Identification
 */

#define	TRIG_MAGIC	0x47495254	/* "TRIG" In File Byte Order */
#define	TRIG_VERSION	1
#define	TRIG_PATH_LEN	256		/* Watched / Changed Path Length */
#define	TRIG_RUN_LEN	512		/* Program & Arguments Length */
#define	TRIG_DEF_MAX	64		/* Default Entry Slots */
#define	TRIG_DEF_DEBOUNCE 100		/* Default Debounce Milliseconds */

/*
 *	Entry Flags	( trig_ent_t.flags )
 */

#define	TRIG_F_USED	0x00000001	/* Entry Slot In Use */
#define	TRIG_F_RECURSIVE 0x00000002	/* Watch Whole Directory Tree */
#define	TRIG_F_SHELL	0x00000004	/* Run Via $SHELL -c run trigger path */
#define	TRIG_F_DISABLED	0x00000008	/* Entry Is Not Watched */
#define	TRIG_F_GONE	0x00010000	/* Server: Watched Path Missing/Deleted */
#define	TRIG_F_RUNNING	0x00020000	/* Server: Child Is Running */
#define	TRIG_F_USER	0x0000FFFF	/* Flags Owned By The Writer */

/*
 *	Control Word Commands	( trig_hdr_t.control, cleared by server )
 */

#define	TRIG_CTL_NONE	0
#define	TRIG_CTL_RELOAD	1		/* Remap File & Rebuild All Watches */
#define	TRIG_CTL_PAUSE	2		/* Keep Watching, Do Not Spawn */
#define	TRIG_CTL_RESUME	3		/* Resume Spawning */
#define	TRIG_CTL_STOP	4		/* Clean Exit */

/*
 *	Server States	( trig_hdr_t.state )
 */

#define	TRIG_ST_STOPPED	0
#define	TRIG_ST_RUNNING	1
#define	TRIG_ST_PAUSED	2

/*
 *	The .trigmem File Header	( 128 Bytes )
 */

typedef	struct	trig_hdr_s	{
	u32	magic;			/* TRIG_MAGIC */
	u32	version;		/* TRIG_VERSION */
	u32	hdr_size;		/* sizeof( trig_hdr_t ) */
	u32	ent_size;		/* sizeof( trig_ent_t ) */
	u32	TraceLevel;		/* Server's Live Trace Level (TracePointer) */
	u32	control;		/* TRIG_CTL_* Command, Server Clears */
	u32	state;			/* TRIG_ST_* Server State */
	u32	generation;		/* Bumped On Each Server (Re)Load */
	u32	count;			/* Entries In Use ( Highest Used + 1 ) */
	u32	max;			/* Entry Slots In File */
	u32	debounce_ms;		/* Default Debounce For Entries With 0 */
	u32	overflows;		/* IN_Q_OVERFLOW Count */
	s32	pid;			/* Server PID, 0 When Not Running */
	u32	spare0;
	u64	started;		/* Server Start Time (usec since epoch) */
	u64	heartbeat;		/* Last Main Loop Pass (usec since epoch) */
	u64	events;			/* Events Delivered */
	u64	spawns;			/* Programs Started */
	u32	spare[ 10 ];
}	trig_hdr_t;

/*
 *	One Watched File Or Directory	( 1088 Bytes )
 */

typedef	struct	trig_ent_s	{
	u32	flags;			/* TRIG_F_* */
	u32	events;			/* inotify IN_* Event Mask To Trigger On */
	u32	debounce_ms;		/* Quiet Time Before Running, 0 = Header Default */
	u32	nwatch;			/* Server: inotify Watches For This Entry */
	s32	last_pid;		/* Server: Last Child PID */
	s32	last_status;		/* Server: Last Child wait() Status, -1 = Running */
	u32	last_mask;		/* Server: Last Triggering Event Mask */
	u32	errors;			/* Server: fork/exec/watch Errors */
	u64	fired;			/* Server: Triggers Delivered */
	u64	last_fired;		/* Server: Last Trigger (usec since epoch) */
	u64	spawns;			/* Server: Programs Started */
	u64	spare;
	char	path[ TRIG_PATH_LEN ];	/* File Or Directory To Watch */
	char	run[ TRIG_RUN_LEN ];	/* Program [ args ] To fork & exec */
	char	last_path[ TRIG_PATH_LEN ];	/* Server: Last Changed Path */
}	trig_ent_t;

#define	TRIG_ENTRY( h, i )	( &((trig_ent_t *)( &((u8 *)( h ))[ (h)->hdr_size ] ))[ i ] )
#define	TRIG_FILE_SIZE( n )	( sizeof( trig_hdr_t ) + ( (u64)( n ) * sizeof( trig_ent_t ) ) )

/*
 *	Library Watcher (opaque) & Callback
 *
 *	The callback receives the tag given to Trig_Add( ), the full path
 *	of what changed, and the inotify mask (IN_Q_OVERFLOW means events
 *	were lost and the path is the watched root, so rescan it).
 */

typedef	struct	trig_w_s	trig_w_t;
typedef	void	(*trig_cb_t)( s32 tag, char * path, u32 mask, void * arg );

/*
 *	Function Prototypes
 */

extern	trig_w_t *	Trig_Open( void );
extern	int		Trig_Fd( trig_w_t * w );
extern	int		Trig_Add( trig_w_t * w, char * path, u32 events, u32 flags, u32 debounce_ms, s32 tag );
extern	int		Trig_Poll( trig_w_t * w, int timeout_ms, trig_cb_t cb, void * arg );
extern	int		Trig_Watches( trig_w_t * w, s32 tag );
extern	void		Trig_Close( trig_w_t * w );

extern	pid_t		Trig_Fork_Watch( char * path, u32 events, u32 flags, u32 debounce_ms, trig_cb_t cb, void * arg );
extern	int		Trig_Stop_Watch( pid_t pid );

extern	u32		Trig_Event_Mask( char * names );
extern	char	*	Trig_Event_Names( u32 mask, char * buf, int size );

extern	int		Trig_Mem_Create( char * file, u32 max, u32 debounce_ms );
extern	trig_hdr_t *	Trig_Mem_Map( char * file, u64 * size );
extern	int		Trig_Mem_Grow( char * file, trig_hdr_t ** hp, u64 * size, u32 max );
extern	void		Trig_Mem_Unmap( trig_hdr_t * h, u64 size );

extern	u64		Trig_Time( void );	/* usec since epoch */
extern	u64		Trig_Mono( void );	/* monotonic msec */

#if	defined( __cplusplus )
}
#endif

#endif
