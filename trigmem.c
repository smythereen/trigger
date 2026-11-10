/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Shared Memory (.trigmem) File Tool
 *
 *	Builds, edits, lists, and controls a trigger server's .trigmem file.
 ******************************************************************************/
#include	"triglocal.h"

static	void	usage( char * av[ ] )
{
	fprintf( stderr,
		 "\n\tTrigger Shared Memory File Tool\n\n"
		 "Usage:\n"
		 "\t%s\tc[reate] file [ max_entries [ debounce_ms ] ]\n"
		 "\t\ta[dd]    file path events flags debounce_ms program [ args ]\n"
		 "\t\tl[oad]   file text_file          (lines like add, # comments)\n"
		 "\t\td[el]    file entry_number\n"
		 "\t\tp[rint]  file                    (list header & entries)\n"
		 "\t\tt[race]  file level              (live server trace level)\n"
		 "\t\tctl      file reload|pause|resume|stop\n"
		 "\t\tw[ait]   file running|stopped [ seconds ]\n"
		 "\n"
		 "\tevents: create write modify delete move moved_from moved_to attrib\n"
		 "\t\tself changes all or 0x hex, joined with ','\n"
		 "\tflags:  r = recursive tree, s = run via $SHELL -c, d = disabled, - = none\n"
		 "\tThe program gets the changed path as its last argument ($1 with s).\n"
		 "\n",
		 av[ 0 ]
	);
	fflush( stderr );
	exit( 1 );
}

static	trig_hdr_t *	h;
static	u64		size;

static	void	Open( char * file )
{
	if( ( h = Trig_Mem_Map( file, &size ) ) == NULL ) {
		fprintf( stderr, "trigmem: cannot map %s\n", file );
		exit( 2 );
	}
}

static	void	Notify( void )
{
	if( h->pid > 0 && kill( h->pid, 0 ) == 0 ) {
		TRIG_STORE( &h->control, TRIG_CTL_RELOAD );
		Trace( TRIG_T_TOOL, "trigmem: asked server %d to reload", h->pid );
	}
}

static	u32	Flags( char * s )
{
	u32	f = TRIG_F_USED;

	for( ; *s != 0; s++ ) {
		switch( *s ) {
		case 'r':	f |= TRIG_F_RECURSIVE;	break;
		case 's':	f |= TRIG_F_SHELL;	break;
		case 'd':	f |= TRIG_F_DISABLED;	break;
		case '-':				break;
		default:	return( 0 );
		}
	}
	return( f );
}

static	int	Add( char * file, char * path, char * events, char * flags, char * deb, char * run )
{
	trig_ent_t *	e;
	u32		ev = Trig_Event_Mask( events );
	u32		fl = Flags( flags );
	u32		i;

	if( ev == 0 || fl == 0 || strlen( path ) >= TRIG_PATH_LEN || strlen( run ) >= TRIG_RUN_LEN || *run == 0 ) {
		fprintf( stderr, "trigmem: bad entry: %s %s %s %s %s\n", path, events, flags, deb, run );
		return( -1 );
	}
	for( i = 0; i < h->count; i++ ) {
		if( ( TRIG_ENTRY( h, i )->flags & TRIG_F_USED ) == 0 ) break;
	}
	if( i >= h->max && Trig_Mem_Grow( file, &h, &size, h->max * 2 ) == -1 ) {
		fprintf( stderr, "trigmem: cannot grow %s\n", file );
		return( -1 );
	}
	e = TRIG_ENTRY( h, i );
	memset( e, 0, sizeof( *e ) );
	e->events = ev;
	e->debounce_ms = (u32)strtoul( deb, NULL, 0 );
	snprintf( e->path, sizeof( e->path ), "%s", path );
	snprintf( e->run, sizeof( e->run ), "%s", run );
	TRIG_STORE( &e->flags, fl );
	if( i >= h->count ) h->count = i + 1;
	printf( "entry %u: %s\n", i, path );
	return( 0 );
}

static	int	Load( char * file, char * text )
{
	FILE *	fp;
	int	bad = 0;
	char	line[ 2048 ];

	if( ( fp = fopen( text, "r" ) ) == NULL ) {
		fprintf( stderr, "trigmem: cannot read %s\n", text );
		return( -1 );
	}
	while( fgets( line, sizeof( line ), fp ) != NULL ) {
		char *	f[ 4 ];
		char *	deb = "0";
		char *	cp = line;

		line[ strcspn( line, "\r\n" ) ] = 0;
		while( *cp == ' ' || *cp == '\t' ) cp++;
		if( *cp == '#' || *cp == 0 ) continue;
		for( int i = 0; i < 4; i++ ) {
			f[ i ] = cp;
			cp += strcspn( cp, " \t" );
			if( *cp != 0 ) *cp++ = 0;
			while( *cp == ' ' || *cp == '\t' ) cp++;
		}
		if( f[ 3 ][ 0 ] >= '0' && f[ 3 ][ 0 ] <= '9' ) {
			deb = f[ 3 ];			/* Optional debounce_ms */
		} else if( f[ 3 ][ 0 ] != 0 ) {
			if( *cp != 0 ) f[ 3 ][ strlen( f[ 3 ] ) ] = ' ';
			cp = f[ 3 ];			/* No debounce: Rejoin Program */
		}
		if( Add( file, f[ 0 ], f[ 1 ], f[ 2 ], deb, cp ) == -1 ) bad++;
	}
	fclose( fp );
	return( bad ? -1 : 0 );
}

static	char *	When( u64 us, char * buf, int size )
{
	time_t		t = us / 1000000;
	struct	tm	tm;

	if( us == 0 ) return( strcpy( buf, "-" ) );
	strftime( buf, size, "%y-%m-%d %H:%M:%S", localtime_r( &t, &tm ) );
	return( buf );
}

static	void	Print( void )
{
	static	char *	states[ ] = { "stopped", "running", "paused" };
	char	b1[ 64 ];
	char	b2[ 256 ];

	printf( "magic=0x%08X version=%u hdr=%u entry=%u max=%u count=%u debounce=%ums\n",
		h->magic, h->version, h->hdr_size, h->ent_size, h->max, h->count, h->debounce_ms );
	printf( "TraceLevel=0x%08X control=%u state=%s pid=%d generation=%u\n",
		h->TraceLevel, h->control, h->state < 3 ? states[ h->state ] : "?", h->pid, h->generation );
	printf( "started=%s events=%" PRIu64 " spawns=%" PRIu64 " overflows=%u\n",
		When( h->started, b1, sizeof( b1 ) ), h->events, h->spawns, h->overflows );
	for( u32 i = 0; i < h->count; i++ ) {
		trig_ent_t *	e = TRIG_ENTRY( h, i );
		char		st[ 32 ];

		if( ( e->flags & TRIG_F_USED ) == 0 ) continue;
		if( e->last_pid == 0 ) {
			strcpy( st, "-" );
		} else if( e->last_status == -1 ) {
			strcpy( st, "running" );
		} else if( WIFEXITED( e->last_status ) ) {
			snprintf( st, sizeof( st ), "exit %d", WEXITSTATUS( e->last_status ) );
		} else {
			snprintf( st, sizeof( st ), "signal %d", WTERMSIG( e->last_status ) );
		}
		printf( "\n[%u] %s\n", i, e->path );
		printf( "    events=%s flags=%s%s%s%s debounce=%ums watches=%u\n",
			Trig_Event_Names( e->events, b2, sizeof( b2 ) ),
			( e->flags & TRIG_F_RECURSIVE ) ? "r" : "",
			( e->flags & TRIG_F_SHELL ) ? "s" : "",
			( e->flags & TRIG_F_DISABLED ) ? "d" : "",
			( e->flags & TRIG_F_GONE ) ? " GONE" : "",
			e->debounce_ms, e->nwatch );
		printf( "    run=%s\n", e->run );
		printf( "    fired=%" PRIu64 " spawns=%" PRIu64 " errors=%u last=%s pid=%d status=%s\n",
			e->fired, e->spawns, e->errors, When( e->last_fired, b1, sizeof( b1 ) ), e->last_pid, st );
		if( e->last_path[ 0 ] ) {
			printf( "    last_path=%s (%s)\n", e->last_path, Trig_Event_Names( e->last_mask, b2, sizeof( b2 ) ) );
		}
	}
}

static	int	Wait( char * want, char * secs )
{
	u64	end = Trig_Mono( ) + 1000 * ( secs ? strtoul( secs, NULL, 0 ) : 10 );
	int	run = ( *want == 'r' );

	while( Trig_Mono( ) < end ) {
		int	up = ( h->pid > 0 && kill( h->pid, 0 ) == 0 && h->state != TRIG_ST_STOPPED );

		if( up == run && ( !run || h->generation > 0 ) ) return( 0 );
		usleep( 20000 );
	}
	return( 1 );
}

int	main( int ac, char * av[ ] )
{
	int	r = 0;

	LocalName = av[ 0 ];
	if( ac < 3 ) usage( av );
	switch( ( av[ 1 ][ 0 ] == 'c' && av[ 1 ][ 1 ] != 0 && av[ 1 ][ 1 ] != 'r' ) ? 'k' : av[ 1 ][ 0 ] ) {
	case 'c':
		r = Trig_Mem_Create( av[ 2 ],
			ac > 3 ? (u32)strtoul( av[ 3 ], NULL, 0 ) : TRIG_DEF_MAX,
			ac > 4 ? (u32)strtoul( av[ 4 ], NULL, 0 ) : TRIG_DEF_DEBOUNCE );
		if( r == -1 ) fprintf( stderr, "trigmem: cannot create %s\n", av[ 2 ] );
		return( r == -1 ? 2 : 0 );
	case 'a':
		{
			char	run[ TRIG_RUN_LEN ];
			int	l = 0;

			if( ac < 8 ) usage( av );
			Open( av[ 2 ] );
			run[ 0 ] = 0;
			for( int i = 7; i < ac; i++ ) {
				l += snprintf( &run[ l ], sizeof( run ) - l, "%s%s", i > 7 ? " " : "", av[ i ] );
			}
			r = Add( av[ 2 ], av[ 3 ], av[ 4 ], av[ 5 ], av[ 6 ], run );
			Notify( );
		}
		break;
	case 'l':
		if( ac < 4 ) usage( av );
		Open( av[ 2 ] );
		r = Load( av[ 2 ], av[ 3 ] );
		Notify( );
		break;
	case 'd':
		if( ac < 4 ) usage( av );
		Open( av[ 2 ] );
		{
			u32	i = (u32)strtoul( av[ 3 ], NULL, 0 );

			if( i >= h->count ) {
				fprintf( stderr, "trigmem: no entry %u\n", i );
				return( 2 );
			}
			TRIG_STORE( &TRIG_ENTRY( h, i )->flags, 0 );
			while( h->count > 0 && ( TRIG_ENTRY( h, h->count - 1 )->flags & TRIG_F_USED ) == 0 ) h->count -= 1;
		}
		Notify( );
		break;
	case 'p':
		Open( av[ 2 ] );
		Print( );
		break;
	case 't':
		if( ac < 4 ) usage( av );
		Open( av[ 2 ] );
		TRIG_STORE( &h->TraceLevel, (u32)strtoul( av[ 3 ], NULL, 0 ) );
		printf( "TraceLevel=0x%08X\n", h->TraceLevel );
		break;
	case 'k':
		if( ac < 4 ) usage( av );
		Open( av[ 2 ] );
		{
			u32	c;

			switch( av[ 3 ][ 0 ] ) {
			case 'r':	c = ( av[ 3 ][ 2 ] == 's' ) ? TRIG_CTL_RESUME : TRIG_CTL_RELOAD;	break;
			case 'p':	c = TRIG_CTL_PAUSE;	break;
			case 's':	c = TRIG_CTL_STOP;	break;
			default:	usage( av );
			}
			TRIG_STORE( &h->control, c );
		}
		break;
	case 'w':
		if( ac < 4 ) usage( av );
		Open( av[ 2 ] );
		r = Wait( av[ 3 ], ac > 4 ? av[ 4 ] : NULL );
		break;
	default:
		usage( av );
	}
	msync( h, size, MS_SYNC );
	Trig_Mem_Unmap( h, size );
	return( r == 0 ? 0 : 1 );
}
