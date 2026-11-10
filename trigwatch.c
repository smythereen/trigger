/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger inotify Watch Functions
 *		Trig_Open( )
 *		Trig_Fd( )
 *		Trig_Add( )
 *		Trig_Poll( )
 *		Trig_Watches( )
 *		Trig_Close( )
 *
 *	A watcher holds one inotify instance, a list of roots (files or
 *	directories, optionally whole trees), the watch descriptor table,
 *	and a debounce queue.  New subdirectories of recursive roots are
 *	watched as they appear, a deleted or moved root is retried every
 *	second, and an event queue overflow rebuilds every watch.
 ******************************************************************************/
#include	"triglocal.h"

#define	SELF_EVENTS	( IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT )

trig_w_t *	Trig_Open( void )
{
	trig_w_t *	w;

	if( ( w = calloc( 1, sizeof( struct trig_w_s ) ) ) == NULL ) return( NULL );
	if( ( w->fd = inotify_init1( IN_CLOEXEC | IN_NONBLOCK ) ) == -1 ) {
		Trace( 0, "Trig_Open: inotify_init1( ) errno=%d - %s", errno, strerror( errno ) );
		free( w );
		return( NULL );
	}
	Trace( TRIG_TRC_LVL, "Trig_Open: inotify fd=%d", w->fd );
	return( w );
}

int	Trig_Fd( trig_w_t * w )
{
	return( w == NULL ? -1 : w->fd );
}

static	int	Wd_Insert( trig_w_t * w, s32 wd, s32 root, char * path )
{
	trig_wd_t *	p;

	for( int i = 0; i < w->nwd; i++ ) {
		p = &w->wds[ i ];
		if( p->wd == wd && p->root == root ) {
			snprintf( p->path, sizeof( p->path ), "%s", path );
			return( 0 );
		}
	}
	if( w->nwd >= w->maxwd ) {
		int	n = ( w->maxwd == 0 ? 64 : w->maxwd * 2 );

		if( ( p = realloc( w->wds, n * sizeof( trig_wd_t ) ) ) == NULL ) return( -1 );
		w->wds = p;
		w->maxwd = n;
	}
	p = &w->wds[ w->nwd++ ];
	p->wd = wd;
	p->root = root;
	snprintf( p->path, sizeof( p->path ), "%s", path );
	return( 1 );
}

static	void	Wd_Delete( trig_w_t * w, int i )
{
	w->wds[ i ] = w->wds[ --w->nwd ];
}

static	int	Wd_Used( trig_w_t * w, s32 wd )
{
	for( int i = 0; i < w->nwd; i++ ) {
		if( w->wds[ i ].wd == wd ) return( 1 );
	}
	return( 0 );
}

/*
 *	Remove All Watches For A Root ( prefix == NULL ) Or Under A Path
 */

static	void	Wd_Remove( trig_w_t * w, s32 root, char * prefix )
{
	int	l = ( prefix != NULL ? strlen( prefix ) : 0 );

	for( int i = 0; i < w->nwd; ) {
		trig_wd_t *	p = &w->wds[ i ];
		s32		wd = p->wd;

		if( p->root != root ||
		    ( prefix != NULL &&
		      ( strncmp( p->path, prefix, l ) != 0 ||
		        ( p->path[ l ] != 0 && p->path[ l ] != '/' ) ) ) ) {
			i++;
			continue;
		}
		Trace( TRIG_TRC_LVL, "Trig: unwatch wd=%d %s", wd, p->path );
		Wd_Delete( w, i );
		if( !Wd_Used( w, wd ) ) inotify_rm_watch( w->fd, wd );
	}
}

static	void	Pend( trig_w_t * w, s32 root, char * path, u32 mask, trig_cb_t cb, void * arg );

/*
 *	Watch One Directory/File, And Its Subdirectories When Recursive
 *	Returns Watches Added, -1 On Error For The Top Path
 */

static	int	Watch_Path( trig_w_t * w, s32 root, char * path, int top, int synth, trig_cb_t cb, void * arg )
{
	trig_root_t *	r = &w->roots[ root ];
	u32		kmask;
	s32		wd;
	int		n;
	DIR	*	d;
	struct	dirent * de;
	struct	stat	st;
	char		sub[ TRIG_LONG_PATH ];

	if( stat( path, &st ) == -1 ) {
		if( top ) Trace( TRIG_TRC_LVL, "Trig: stat( %s ) errno=%d - %s", path, errno, strerror( errno ) );
		return( -1 );
	}
	kmask = ( r->events & IN_ALL_EVENTS ) | SELF_EVENTS | IN_MASK_ADD;
	if( S_ISDIR( st.st_mode ) ) {
		kmask |= IN_ONLYDIR;
		if( ( r->flags & TRIG_F_RECURSIVE ) != 0 ) kmask |= IN_CREATE | IN_MOVED_TO | IN_MOVED_FROM;
	}
	if( top ) r->isdir = S_ISDIR( st.st_mode );
	if( ( wd = inotify_add_watch( w->fd, path, kmask ) ) == -1 ) {
		Trace( 0, "Trig: inotify_add_watch( %s, 0x%08X ) errno=%d - %s", path, kmask, errno, strerror( errno ) );
		return( -1 );
	}
	if( top ) r->wd = wd;
	n = ( Wd_Insert( w, wd, root, path ) >= 0 ? 1 : 0 );
	Trace( TRIG_TRC_LVL, "Trig: watch wd=%d mask=0x%08X tag=%d %s", wd, kmask, r->tag, path );
	if( !S_ISDIR( st.st_mode ) ) return( n );
	if( ( r->flags & TRIG_F_RECURSIVE ) == 0 && !synth ) return( n );
	if( ( d = opendir( path ) ) == NULL ) return( n );
	while( ( de = readdir( d ) ) != NULL ) {
		if( strcmp( de->d_name, "." ) == 0 || strcmp( de->d_name, ".." ) == 0 ) continue;
		snprintf( sub, sizeof( sub ), "%s/%s", path, de->d_name );
		if( lstat( sub, &st ) == -1 ) continue;
		if( S_ISDIR( st.st_mode ) ) {
			if( ( r->flags & TRIG_F_RECURSIVE ) == 0 ) continue;
			if( synth && ( r->events & IN_CREATE ) != 0 ) Pend( w, root, sub, IN_CREATE | IN_ISDIR, cb, arg );
			if( ( wd = Watch_Path( w, root, sub, 0, synth, cb, arg ) ) > 0 ) n += wd;
		} else if( synth && ( r->events & ( IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO ) ) != 0 ) {
			/* Files Created Before The New Directory Was Watched */
			Pend( w, root, sub, ( r->events & IN_CREATE ) != 0 ? IN_CREATE : IN_CLOSE_WRITE, cb, arg );
		}
	}
	closedir( d );
	return( n );
}

int	Trig_Add( trig_w_t * w, char * path, u32 events, u32 flags, u32 debounce_ms, s32 tag )
{
	trig_root_t *	r;
	int		l;

	if( w == NULL || path == NULL || ( l = strlen( path ) ) == 0 || l >= TRIG_PATH_LEN ) {
		errno = EINVAL;
		return( -1 );
	}
	if( w->nroot >= w->maxroot ) {
		int	n = ( w->maxroot == 0 ? 16 : w->maxroot * 2 );

		if( ( r = realloc( w->roots, n * sizeof( trig_root_t ) ) ) == NULL ) return( -1 );
		w->roots = r;
		w->maxroot = n;
	}
	r = &w->roots[ w->nroot ];
	memset( r, 0, sizeof( *r ) );
	snprintf( r->path, sizeof( r->path ), "%s", path );
	while( l > 1 && r->path[ l - 1 ] == '/' ) r->path[ --l ] = 0;
	r->events = events;
	r->flags = flags;
	r->debounce_ms = debounce_ms;
	r->tag = tag;
	r->wd = -1;
	w->nroot += 1;
	return( Watch_Path( w, w->nroot - 1, r->path, 1, 0, NULL, NULL ) );
}

int	Trig_Watches( trig_w_t * w, s32 tag )
{
	int	n = 0;

	for( int i = 0; i < w->nwd; i++ ) {
		if( w->roots[ w->wds[ i ].root ].tag == tag ) n++;
	}
	return( n );
}

/*
 *	Debounce Queue
 */

static	void	Fire( trig_w_t * w, int i, trig_cb_t cb, void * arg )
{
	trig_pend_t	p = w->pend[ i ];

	w->pend[ i ] = w->pend[ --w->npend ];
	Trace( TRIG_TRC_LVL, "Trig: fire tag=%d mask=0x%08X %s", w->roots[ p.root ].tag, p.mask, p.path );
	if( cb != NULL ) ( *cb )( w->roots[ p.root ].tag, p.path, p.mask, arg );
}

static	void	Pend( trig_w_t * w, s32 root, char * path, u32 mask, trig_cb_t cb, void * arg )
{
	trig_root_t *	r = &w->roots[ root ];
	trig_pend_t *	p;
	int		old = 0;

	for( int i = 0; i < w->npend; i++ ) {
		p = &w->pend[ i ];
		if( p->root == root && strcmp( p->path, path ) == 0 ) {
			p->mask |= mask;
			p->due = Trig_Mono( ) + r->debounce_ms;
			Trace( TRIG_TRC_LVL, "Trig: debounce tag=%d mask=0x%08X %s", r->tag, p->mask, path );
			return;
		}
		if( p->due < w->pend[ old ].due ) old = i;
	}
	if( w->npend >= TRIG_MAX_PEND ) Fire( w, old, cb, arg );
	p = &w->pend[ w->npend++ ];
	p->root = root;
	p->mask = mask;
	p->due = Trig_Mono( ) + r->debounce_ms;
	snprintf( p->path, sizeof( p->path ), "%s", path );
	if( r->debounce_ms == 0 ) Fire( w, w->npend - 1, cb, arg );
}

/*
 *	Rebuild Every Watch ( After IN_Q_OVERFLOW )
 */

static	void	Rescan( trig_w_t * w, trig_cb_t cb, void * arg )
{
	for( int i = 0; i < w->nwd; i++ ) inotify_rm_watch( w->fd, w->wds[ i ].wd );
	w->nwd = 0;
	for( int r = 0; r < w->nroot; r++ ) {
		w->roots[ r ].wd = -1;
		Watch_Path( w, r, w->roots[ r ].path, 1, 0, cb, arg );
		if( cb != NULL ) ( *cb )( w->roots[ r ].tag, w->roots[ r ].path, IN_Q_OVERFLOW, arg );
	}
}

static	void	Event( trig_w_t * w, struct inotify_event * ip, trig_cb_t cb, void * arg )
{
	trig_wd_t *	snap;
	int		hits = 0;
	char		path[ TRIG_LONG_PATH ];

	if( ip->mask & IN_Q_OVERFLOW ) {
		Trace( 0, "Trig: IN_Q_OVERFLOW - rescanning %d roots", w->nroot );
		Rescan( w, cb, arg );
		return;
	}
	if( ip->mask & IN_IGNORED ) {
		for( int i = 0; i < w->nwd; ) {
			if( w->wds[ i ].wd != ip->wd ) {
				i++;
				continue;
			}
			Trace( TRIG_TRC_LVL, "Trig: IN_IGNORED wd=%d %s", ip->wd, w->wds[ i ].path );
			if( w->roots[ w->wds[ i ].root ].wd == ip->wd ) w->roots[ w->wds[ i ].root ].wd = -1;
			Wd_Delete( w, i );
		}
		return;
	}

	/*
	 *	Snapshot The Matching Watches, Handlers Change The Table
	 */

	if( ( snap = malloc( ( w->nwd + 1 ) * sizeof( trig_wd_t ) ) ) == NULL ) return;
	for( int i = 0; i < w->nwd; i++ ) {
		if( w->wds[ i ].wd == ip->wd ) snap[ hits++ ] = w->wds[ i ];
	}
	if( hits == 0 ) Trace( TRIG_TRC_LVL, "Trig: stale wd=%d mask=0x%08X", ip->wd, ip->mask );
	for( int i = 0; i < hits; i++ ) {
		trig_wd_t *	p = &snap[ i ];
		s32		root = p->root;
		trig_root_t *	r = &w->roots[ root ];

		if( ip->len > 0 && ip->name[ 0 ] != 0 ) {
			snprintf( path, sizeof( path ), "%s/%s", p->path, ip->name );
		} else {
			snprintf( path, sizeof( path ), "%s", p->path );
		}
		if( ( ip->mask & SELF_EVENTS ) != 0 ) {
			if( r->wd != p->wd ) continue;		/* Subdir: Parent Reports It */
			Trace( 0, "Trig: watched root %s %s - will retry", r->path,
				( ip->mask & IN_DELETE_SELF ) ? "deleted" : ( ip->mask & IN_MOVE_SELF ) ? "moved" : "unmounted" );
			Wd_Remove( w, root, NULL );
			r->wd = -1;
			if( cb != NULL ) ( *cb )( r->tag, r->path, ip->mask, arg );
			continue;
		}
		if( ( r->flags & TRIG_F_RECURSIVE ) != 0 && ( ip->mask & IN_ISDIR ) != 0 ) {
			if( ip->mask & ( IN_CREATE | IN_MOVED_TO ) ) {
				if( ( ip->mask & r->events & IN_ALL_EVENTS ) != 0 ) Pend( w, root, path, ip->mask, cb, arg );
				Trace( TRIG_TRC_LVL, "Trig: new subdir %s", path );
				Watch_Path( w, root, path, 0, 1, cb, arg );
				continue;
			} else if( ip->mask & ( IN_MOVED_FROM | IN_DELETE ) ) {
				Wd_Remove( w, root, path );
			}
		}
		if( ( ip->mask & r->events & IN_ALL_EVENTS ) != 0 ) {
			Pend( w, root, path, ip->mask, cb, arg );
		}
	}
	free( snap );
}

static	void	Retry( trig_w_t * w, trig_cb_t cb, void * arg )
{
	u64	now = Trig_Mono( );

	if( now < w->retry ) return;
	w->retry = now + 1000;
	for( int r = 0; r < w->nroot; r++ ) {
		if( w->roots[ r ].wd != -1 ) continue;
		if( access( w->roots[ r ].path, F_OK ) != 0 ) continue;
		if( Watch_Path( w, r, w->roots[ r ].path, 1, w->roots[ r ].isdir, cb, arg ) > 0 ) {
			Trace( 0, "Trig: watched root %s is back", w->roots[ r ].path );
		}
	}
}

int	Trig_Poll( trig_w_t * w, int timeout_ms, trig_cb_t cb, void * arg )
{
	struct	pollfd	pfd;
	u64		now;
	int		len;
	int		n;
	char		buf[ 65536 ] __attribute__(( aligned( __alignof__( struct inotify_event ) ) ));

	if( w == NULL ) return( -1 );
	now = Trig_Mono( );
	for( int i = 0; i < w->npend; i++ ) {
		int	t = ( w->pend[ i ].due > now ? (int)( w->pend[ i ].due - now ) : 0 );

		if( timeout_ms < 0 || t < timeout_ms ) timeout_ms = t;
	}
	for( int r = 0; r < w->nroot; r++ ) {
		if( w->roots[ r ].wd == -1 && ( timeout_ms < 0 || timeout_ms > 1000 ) ) timeout_ms = 1000;
	}
	pfd.fd = w->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	n = poll( &pfd, 1, timeout_ms );
	if( n == -1 && errno != EINTR ) return( -1 );
	if( n > 0 && ( pfd.revents & POLLIN ) != 0 ) {
		while( ( len = read( w->fd, buf, sizeof( buf ) ) ) > 0 ) {
			for( int o = 0; o + (int)sizeof( struct inotify_event ) <= len; ) {
				struct	inotify_event *	ip = (struct inotify_event *)&buf[ o ];

				Trace( TRIG_TRC_LVL, "Trig: event wd=%d mask=0x%08X cookie=%u name=%s",
					ip->wd, ip->mask, ip->cookie, ip->len ? ip->name : "" );
				Event( w, ip, cb, arg );
				o += sizeof( struct inotify_event ) + ip->len;
			}
		}
	}
	Retry( w, cb, arg );
	now = Trig_Mono( );
	n = 0;
	for( int i = 0; i < w->npend; ) {
		if( w->pend[ i ].due <= now ) {
			Fire( w, i, cb, arg );
			n++;
		} else {
			i++;
		}
	}
	return( n );
}

void	Trig_Close( trig_w_t * w )
{
	if( w == NULL ) return;
	Trace( TRIG_TRC_LVL, "Trig_Close: fd=%d roots=%d watches=%d", w->fd, w->nroot, w->nwd );
	close( w->fd );
	free( w->roots );
	free( w->wds );
	free( w );
}
