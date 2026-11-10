/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Server Program
 *
 *	Usage:	trigger <.trigmem> [ trace_file ]
 *
 *	Maps the .trigmem file, watches each entry's file or directory
 *	(tree), and fork & execs the entry's program with the changed
 *	path as its last argument.  The header's TraceLevel is the live
 *	trace level and its control word reloads, pauses, resumes, or
 *	stops the server.
 ******************************************************************************/
#include	"triglocal.h"

static	char		*	file;
static	trig_hdr_t	*	hdr;
static	u64			size;
static	trig_w_t	*	w;
static	volatile sig_atomic_t	got_chld;
static	volatile sig_atomic_t	got_stop;
static	volatile sig_atomic_t	got_hup;

static	void	usage( char * av[ ] )
{
	fprintf( stderr,
		 "Usage:\n"
		 "\t%s\t<.trigmem> [ trace_file ]\n"
		 "\n"
		 "\tBuild the .trigmem file with trigmem(1).\n"
		 "\n",
		 av[ 0 ]
	);
	fflush( stderr );
	exit( 1 );
}

static	void	Sig( int sig )
{
	switch( sig ) {
	case SIGCHLD:	got_chld = 1;	break;
	case SIGHUP:	got_hup = 1;	break;
	default:	got_stop = 1;	break;
	}
}

static	void	Spawn_Entry( int idx, trig_ent_t * e, char * path, u32 mask )
{
	pid_t	pid;
	char	names[ 256 ];

	Trig_Event_Names( mask, names, sizeof( names ) );
	switch( pid = fork( ) ) {
	case -1:
		e->errors += 1;
		Trace( 0, "Entry %d: fork( ) errno=%d - %s", idx, errno, strerror( errno ) );
		return;
	case 0:
		{
			char *	argv[ 64 ];
			char	run[ TRIG_RUN_LEN ];
			char	num[ 32 ];
			char *	sp;
			int	n = 0;

			signal( SIGCHLD, SIG_DFL );
			signal( SIGTERM, SIG_DFL );
			signal( SIGINT, SIG_DFL );
			signal( SIGHUP, SIG_DFL );
			TracePointer = NULL;
			setenv( "TRIG_PATH", path, 1 );
			setenv( "TRIG_EVENTS", names, 1 );
			snprintf( num, sizeof( num ), "0x%08X", mask );
			setenv( "TRIG_MASK", num, 1 );
			snprintf( num, sizeof( num ), "%d", idx );
			setenv( "TRIG_ENTRY", num, 1 );
			setenv( "TRIG_WATCH", e->path, 1 );
			snprintf( run, sizeof( run ), "%s", e->run );
			if( e->flags & TRIG_F_SHELL ) {
				char *	shell = getenv( "SHELL" );

				if( shell == NULL || *shell == 0 ) shell = "/bin/sh";
				argv[ n++ ] = shell;
				argv[ n++ ] = "-c";
				argv[ n++ ] = run;
				argv[ n++ ] = "trigger";
			} else {
				for( char * t = strtok_r( run, " \t", &sp ); t != NULL && n < 60; t = strtok_r( NULL, " \t", &sp ) ) {
					argv[ n++ ] = t;
				}
			}
			argv[ n++ ] = path;
			argv[ n ] = NULL;
			execvp( argv[ 0 ], argv );
			Trace( 0, "Entry %d: execvp( %s ) errno=%d - %s", idx, argv[ 0 ], errno, strerror( errno ) );
			_exit( 127 );
		}
	default:
		e->last_pid = pid;
		e->last_status = -1;
		e->flags |= TRIG_F_RUNNING;
		e->spawns += 1;
		hdr->spawns += 1;
		Trace( TRIG_T_SPAWN, "Entry %d: spawned pid=%d %s%s %s",
			idx, pid, ( e->flags & TRIG_F_SHELL ) ? "$SHELL -c " : "", e->run, path );
	}
}

static	void	On_Event( s32 idx, char * path, u32 mask, void * arg )
{
	trig_ent_t *	e;
	char		names[ 256 ];

	if( idx < 0 || (u32)idx >= hdr->count ) return;
	e = TRIG_ENTRY( hdr, idx );
	Trig_Event_Names( mask, names, sizeof( names ) );
	e->fired += 1;
	e->last_fired = Trig_Time( );
	e->last_mask = mask;
	snprintf( e->last_path, sizeof( e->last_path ), "%s", path );
	hdr->events += 1;
	if( mask & IN_Q_OVERFLOW ) hdr->overflows += 1;
	if( mask & ( IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT ) ) {
		e->flags |= TRIG_F_GONE;
		e->nwatch = 0;
	} else {
		e->flags &= ~TRIG_F_GONE;
	}
	Trace( TRIG_T_EVENT, "Entry %d: event %s %s", idx, names, path );
	if( hdr->state == TRIG_ST_PAUSED ) {
		Trace( TRIG_T_EVENT, "Entry %d: paused - not spawning", idx );
		return;
	}
	if( ( mask & e->events & IN_ALL_EVENTS ) == 0 && ( mask & IN_Q_OVERFLOW ) == 0 ) return;
	Spawn_Entry( idx, e, path, mask );
}

static	void	Reap( void )
{
	pid_t	pid;
	int	st;

	got_chld = 0;
	while( ( pid = waitpid( -1, &st, WNOHANG ) ) > 0 ) {
		for( u32 i = 0; i < hdr->count; i++ ) {
			trig_ent_t *	e = TRIG_ENTRY( hdr, i );

			if( e->last_pid != pid ) continue;
			e->last_status = st;
			e->flags &= ~TRIG_F_RUNNING;
			if( WIFEXITED( st ) && WEXITSTATUS( st ) == 127 ) e->errors += 1;
			break;
		}
		if( WIFEXITED( st ) ) {
			Trace( TRIG_T_CHILD, "Child pid=%d exit=%d", pid, WEXITSTATUS( st ) );
		} else if( WIFSIGNALED( st ) ) {
			Trace( TRIG_T_CHILD, "Child pid=%d signal=%d", pid, WTERMSIG( st ) );
		}
	}
}

static	int	Load( void )
{
	int	n = 0;

	if( w != NULL ) Trig_Close( w );
	if( ( w = Trig_Open( ) ) == NULL ) return( -1 );
	hdr->generation += 1;
	for( u32 i = 0; i < hdr->count && i < hdr->max; i++ ) {
		trig_ent_t *	e = TRIG_ENTRY( hdr, i );
		u32		deb;
		int		r;

		e->flags &= ~( TRIG_F_GONE | TRIG_F_RUNNING );
		e->nwatch = 0;
		if( ( e->flags & TRIG_F_USED ) == 0 || ( e->flags & TRIG_F_DISABLED ) != 0 ) continue;
		e->path[ TRIG_PATH_LEN - 1 ] = 0;
		e->run[ TRIG_RUN_LEN - 1 ] = 0;
		deb = ( e->debounce_ms != 0 ? e->debounce_ms : hdr->debounce_ms );
		r = Trig_Add( w, e->path, e->events, e->flags, deb, (s32)i );
		if( r < 0 ) {
			e->flags |= TRIG_F_GONE;
			e->errors += 1;
			Trace( 0, "Entry %u: cannot watch %s (will retry)", i, e->path );
		} else {
			e->nwatch = r;
			n += 1;
		}
		Trace( TRIG_T_WATCH, "Entry %u: %s events=0x%08X flags=0x%X debounce=%u watches=%d run=%s",
			i, e->path, e->events, e->flags, deb, r, e->run );
	}
	Trace( TRIG_T_CTL, "Loaded generation %u: %d of %u entries watched", hdr->generation, n, hdr->count );
	return( n );
}

static	int	Remap( void )
{
	trig_hdr_t *	h;
	u64		sz;
	u32		tl = hdr->TraceLevel;
	u32		st = hdr->state;

	if( ( h = Trig_Mem_Map( file, &sz ) ) == NULL ) return( -1 );
	TracePointer = &h->TraceLevel;
	Trig_Mem_Unmap( hdr, size );
	hdr = h;
	size = sz;
	hdr->TraceLevel = ( hdr->TraceLevel != 0 ? hdr->TraceLevel : tl );
	hdr->state = st;
	hdr->pid = getpid( );
	return( 0 );
}

static	void	Stop_Server( int code ) __attribute__(( noreturn ));
static	void	Stop_Server( int code )
{
	Trace( 0, "%s: stopping (events=%" PRIu64 " spawns=%" PRIu64 ")", LocalName, hdr->events, hdr->spawns );
	Reap( );
	Trig_Close( w );
	hdr->state = TRIG_ST_STOPPED;
	hdr->pid = 0;
	TracePointer = NULL;
	msync( hdr, size, MS_SYNC );
	Trig_Mem_Unmap( hdr, size );
	exit( code );
}

int	main( int ac, char * av[ ] )
{
	struct	sigaction	sa;
	char		*	tf = "trigger.trc";

	LocalName = av[ 0 ];
	switch( ac ) {
	case 3:		tf = av[ 2 ];
			/* FALLTHRU */
	case 2:		file = av[ 1 ];
			break;
	default:	usage( av );
	}
	if( ( hdr = Trig_Mem_Map( file, &size ) ) == NULL ) {
		fprintf( stderr, "%s: %s is not a usable .trigmem file\n", av[ 0 ], file );
		exit( 2 );
	}
	if( hdr->pid > 0 && hdr->pid != getpid( ) && kill( hdr->pid, 0 ) == 0 ) {
		fprintf( stderr, "%s: server pid %d is already running on %s\n", av[ 0 ], hdr->pid, file );
		exit( 3 );
	}
	if( Initialize_Print( tf ) == -1 ) {
		fprintf( stderr, "%s: cannot open trace file %s\n", av[ 0 ], tf );
	}
	TracePointer = &hdr->TraceLevel;

	memset( &sa, 0, sizeof( sa ) );
	sa.sa_handler = Sig;
	sigemptyset( &sa.sa_mask );
	sa.sa_flags = SA_NOCLDSTOP;
	sigaction( SIGCHLD, &sa, NULL );
	sa.sa_flags = 0;
	sigaction( SIGTERM, &sa, NULL );
	sigaction( SIGINT, &sa, NULL );
	sigaction( SIGHUP, &sa, NULL );
	signal( SIGPIPE, SIG_IGN );

	hdr->pid = getpid( );
	hdr->started = Trig_Time( );
	hdr->state = TRIG_ST_RUNNING;
	hdr->control = TRIG_CTL_NONE;
	Trace( 0, "%s: pid %d serving %s (%u entries, trace 0x%08X)", LocalName, hdr->pid, file, hdr->count, hdr->TraceLevel );
	Load( );

	for( ; ; ) {
		u32	ctl;

		New_Err_File( );
		if( got_chld ) Reap( );
		if( got_stop ) Stop_Server( 0 );
		ctl = TRIG_XCHG( &hdr->control, TRIG_CTL_NONE );
		if( got_hup ) {
			got_hup = 0;
			ctl = TRIG_CTL_RELOAD;
		}
		switch( ctl ) {
		case TRIG_CTL_NONE:
			break;
		case TRIG_CTL_RELOAD:
			Trace( TRIG_T_CTL, "Control: reload" );
			if( Remap( ) == -1 ) {
				Trace( 0, "%s: remap of %s failed", LocalName, file );
				Stop_Server( 4 );
			}
			Load( );
			break;
		case TRIG_CTL_PAUSE:
			Trace( TRIG_T_CTL, "Control: pause" );
			hdr->state = TRIG_ST_PAUSED;
			break;
		case TRIG_CTL_RESUME:
			Trace( TRIG_T_CTL, "Control: resume" );
			hdr->state = TRIG_ST_RUNNING;
			break;
		case TRIG_CTL_STOP:
			Trace( TRIG_T_CTL, "Control: stop" );
			Stop_Server( 0 );
			/* FALLTHRU */
		default:
			Trace( 0, "Control: unknown command %u ignored", ctl );
			break;
		}
		hdr->heartbeat = Trig_Time( );
		Trace( TRIG_T_LOOP, "Loop: generation %u state %u", hdr->generation, hdr->state );
		if( Trig_Poll( w, 250, On_Event, NULL ) == -1 && errno != EINTR ) {
			Trace( 0, "%s: poll errno=%d - %s", LocalName, errno, strerror( errno ) );
			Stop_Server( 5 );
		}
	}
	exit( 0 );
}
