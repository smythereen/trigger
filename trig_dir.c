/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2010-2026 John R. Smythe Jr. */

#include	"triglocal.h"
#include	<sys/inotify.h>

static	void	usage( char * av[ ] )
{
	fprintf( stderr,
		 "Usage:\n"
		 "\t%s\ttriggering_directory cmd\n"
		 "\n",
		 av[ 0 ]
	);
	fflush( stderr );
	exit( 1 );
}

#ifdef	JUNK
static	uint32_t	w_mask = (	IN_ALL_EVENTS	);
#endif

static	uint32_t	w_mask = (
					IN_CLOSE_WRITE	|
					IN_CREATE	|
					IN_DELETE_SELF	|
					IN_MOVE_SELF	|
					IN_MOVED_TO	|
					IN_DONT_FOLLOW	|
					IN_ONLYDIR
			);

#define	EVT( x, a )	{ #x, x, a }

static	struct	ev_s	{
	char	*	name;
	uint32_t	mask;
	uint32_t	action;
}	evnts[ ] =	{

/* Special flags.  */

	EVT( IN_ONESHOT,	0 ),	/* 0x80000000 Only send event once.  */
	EVT( IN_ISDIR,		0 ),	/* 0x40000000 Event occurred against dir.  */
	EVT( IN_MASK_ADD,	0 ),	/* 0x20000000 Add to the mask of an already existing watch.  */
	EVT( IN_DONT_FOLLOW,	0 ),	/* 0x02000000 Do not follow a sym link.  */
	EVT( IN_ONLYDIR,	0 ),	/* 0x01000000 Only watch the path if it is a directory.  */

/* Events sent by the kernel.  */
/* the following are legal events.  they are sent as needed to any watch */

	EVT( IN_IGNORED,	0 ),	/* 0x00008000 File was ignored.  */
	EVT( IN_Q_OVERFLOW,	2 ),	/* 0x00004000 Event queued overflowed.  */
	EVT( IN_UNMOUNT,	2 ),	/* 0x00002000 Backing fs was unmounted.  */

/* the following are legal, implemented events that user-space can watch for */

	EVT( IN_MOVE_SELF,	2 ),	/* 0x00000800 Self was moved.  */
	EVT( IN_DELETE_SELF,	2 ),	/* 0x00000400 Self was deleted.  */
	EVT( IN_DELETE,		0 ),	/* 0x00000200 Subfile was deleted.  */
	EVT( IN_CREATE,		0 ),	/* 0x00000100 Subfile was created.  */
	EVT( IN_MOVED_TO,	1 ),	/* 0x00000080 File was moved to Y.  */
	EVT( IN_MOVED_FROM,	0 ),	/* 0x00000040 File was moved from X.  */
	EVT( IN_OPEN,		0 ),	/* 0x00000020 File was opened.  */
	EVT( IN_CLOSE_NOWRITE,	0 ),	/* 0x00000010 Unwrittable file closed.  */
	EVT( IN_CLOSE_WRITE,	1 ),	/* 0x00000008 Writtable file was closed.  */
	EVT( IN_ATTRIB,		0 ),	/* 0x00000004 Metadata changed.  */
	EVT( IN_MODIFY,		0 ),	/* 0x00000002 File was modified.  */
	EVT( IN_ACCESS,		0 ),	/* 0x00000001 File was accessed.  */

	{ NULL, 0, 0 }
};

static	char	e_file[ 2048 ];
static	char *	cwd;

static	int	EV_Trace( struct inotify_event * ip )
{
	uint32_t	a;
	char	*	bp;
struct	ev_s	*	ev;
	char		trc[ 1024 ];

	if( cwd == NULL ) {
		cwd = getcwd( e_file, sizeof( e_file ) );
		while( *cwd != 0 ) cwd++;
		*cwd++ = '/';
	}
	*cwd = 0;
	bp = trc;
	bp += snprintf(
		bp,
		sizeof( trc ),
		"Event: mask:%08X, ",
		ip->mask
	);
	if( ip->cookie > 0 ) {
		bp += snprintf(
			bp,
			sizeof( trc ) - ( bp - trc ),
			"cookie:%08X, ",
			ip->cookie
		);
	}
	if( ip->len > 0 ) {
		snprintf(
			cwd,
			sizeof( e_file ) - ( cwd - e_file ),
			"%s%s",
			ip->name,
			( ( ip->mask & IN_ISDIR ) != 0 ? "/" : "" )
		);
	}
	bp += snprintf(
		bp,
		sizeof( trc ) - ( bp - trc ),
		"%s, ",
		e_file
	);
	bp += snprintf(
		bp,
		sizeof( trc ) - ( bp - trc ),
		"%%s"
	);
	for( ev = evnts, a = 0; ev->mask != 0; ev += 1 ) {
		if( ( ip->mask & ev->mask ) == 0 ) continue;
		bp += snprintf(
			bp,
			sizeof( trc ) - ( bp - trc ),
			" %s",
			ev->name
		);
		a |= ev->action;
	}
	switch( a ) {
	case 0:
		Trace( 0, trc, "Ignoring" );
		return( 0 );
	case 1:
		Trace( 0, trc, "Process " );
		return( 1 );
	default:
		Trace( 0, trc, "Exiting " );
		exit( 3 );
	}
}

int	main( int ac, char * av[ ] )
{
	int			l;
	int			len;
	int			id;
	int			wd[ 10 ];
struct	inotify_event	*	ip;
	char		*	bp;
	char			trc[ 1024 ];
	char			buf[ 4096 ];
	char		*	argv[ 5 ];	/* shell, "-c", cmd, file name, NULL */
extern	char	**	environ;

	LocalName = av[ 0 ];
	if( ac < 3 ) usage( av );	/* directory and cmd are both required */
	switch( ac ) {
	default:
	case  2:
			if( chdir( av[ 1 ] ) != 0 ) {
				printf( "chdir( %s ): errno=%d - %s\n", av[ 1 ], errno, strerror( errno ) );
			} else if( ( id = inotify_init1( IN_CLOEXEC ) ) >= 0 &&
				   ( wd[ 0 ] = inotify_add_watch( id, ".", w_mask ) ) >= 0 ) {
				break;
			} else {
				printf( "inotify( %d, %s ): errno=%d - %s\n", id, av[ 1 ], errno, strerror( errno ) );
			}
	case  1:
	case  0:	usage( av );
	}
	for( bp = getcwd( buf, sizeof( buf ) ); *bp != 0; bp++ ) {
		switch( *bp ) {
		case '/':	*bp = '_';
				break;
		}
	};
	snprintf( trc, sizeof( trc ), "%s.trc", buf );
	Initialize_Print( trc );

	Trace( 0, "%s: watching idirectory %s", av[ 0 ], av[ 1 ] );

	for( ; ; ) {
		memset( buf, 0, sizeof( buf ) );
		if( ( len = read( id, buf, sizeof( buf ) ) ) < 16 ) {
			if( len < 0 ) {
				Trace(	0,
					"ERROR read( id, buf, 16 ): %d - %s",
					errno,
					strerror( errno )
				);
				exit( 7 );
			}
			Trace( 0, "ERROR read( 1d, buf, 16 ) = %d is not >= 16\n", len );
			continue;
		}
		Trace( 0, "read len = %d\n", len );
		New_Err_File( );
		for(	ip = (struct inotify_event *)buf ;
			len >= (int)offsetof( struct inotify_event, name ) ;
			len -= l,
			ip = (struct inotify_event *)( &((u8 *)ip)[ l ] ) ) {
			char *	shell;

			l = offsetof( struct inotify_event, name ) + ip->len;
			if( EV_Trace( ip ) == 0 ) continue;
#ifndef	JUNK
			switch( fork( ) ) {
			case	0:	/* Child */
				if( ( shell = getenv( "SHELL" ) ) == NULL ) shell = "/bin/bash";

				argv[ 0 ] = "-bash";
				argv[ 1 ] = "-c";
				argv[ 2 ] = av[ 2 ];
				argv[ 3 ] = ip->name;
				argv[ 4 ] = NULL;

				execve( shell, argv, environ );
				Trace( 0, "Error execve( ) - errno %d - %s",  errno, strerror( errno ) );
				exit( 2 );

			case	-1:	/* Error - No Child Spawned */
			default:        /* Parent */
				break;
			}
#endif
		}
	}
	exit( 0 );
}
