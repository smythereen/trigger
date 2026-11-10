/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Forked Directory Watcher Functions
 *		Trig_Fork_Watch( )
 *		Trig_Stop_Watch( )
 *
 *	Trig_Fork_Watch( ) forks a child that watches a file or directory
 *	structure and calls the caller's subroutine (in the child) each
 *	time a (debounced) trigger event happens.  The child exits when
 *	the parent dies, or on SIGTERM ( Trig_Stop_Watch( ) ).
 ******************************************************************************/
#include	"triglocal.h"

static	volatile sig_atomic_t	stop;

static	void	Stop( int sig )
{
	stop = 1;
}

pid_t	Trig_Fork_Watch( char * path, u32 events, u32 flags, u32 debounce_ms, trig_cb_t cb, void * arg )
{
	trig_w_t *	w;
	pid_t		pid;
	pid_t		parent = getpid( );

	switch( pid = fork( ) ) {
	case -1:
		Trace( 0, "Trig_Fork_Watch: fork( ) errno=%d - %s", errno, strerror( errno ) );
		return( -1 );
	default:
		Trace( TRIG_TRC_LVL, "Trig_Fork_Watch: pid=%d watching %s", pid, path );
		return( pid );
	case 0:
		break;
	}
	prctl( PR_SET_PDEATHSIG, SIGTERM );
	signal( SIGTERM, Stop );
	signal( SIGINT, SIG_DFL );
	signal( SIGCHLD, SIG_DFL );
	if( getppid( ) != parent ) _exit( 0 );
	if( ( w = Trig_Open( ) ) == NULL ) _exit( 2 );
	if( Trig_Add( w, path, events, flags, debounce_ms, 0 ) < 0 ) {
		Trace( 0, "Trig_Fork_Watch: cannot watch %s", path );
		_exit( 3 );
	}
	while( !stop ) {
		if( Trig_Poll( w, 500, cb, arg ) == -1 ) break;
		if( getppid( ) != parent ) break;
	}
	Trig_Close( w );
	_exit( 0 );
}

int	Trig_Stop_Watch( pid_t pid )
{
	int	status = 0;

	if( pid <= 0 ) return( -1 );
	if( kill( pid, SIGTERM ) == -1 ) return( -1 );
	while( waitpid( pid, &status, 0 ) == -1 ) {
		if( errno != EINTR ) return( -1 );
	}
	Trace( TRIG_TRC_LVL, "Trig_Stop_Watch: pid=%d status=0x%X", pid, status );
	return( status );
}
