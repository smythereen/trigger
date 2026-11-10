/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Library Callback Test Program
 *
 *	Usage:	trigcb fork|poll directory log_file seconds
 *
 *	fork:	Trig_Fork_Watch( ) a recursive watcher that appends each
 *		callback to log_file, sleep, then Trig_Stop_Watch( ).
 *	poll:	Trig_Open( )/Trig_Add( )/Trig_Poll( ) in this process.
 ******************************************************************************/
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/wait.h>
#include	<trigger.h>

static	void	Callback( s32 tag, char * path, u32 mask, void * arg )
{
	FILE *	fp = fopen( (char *)arg, "a" );
	char	names[ 256 ];

	if( fp == NULL ) return;
	fprintf( fp, "cb tag=%d %s %s\n", tag, Trig_Event_Names( mask, names, sizeof( names ) ), path );
	fclose( fp );
}

int	main( int ac, char * av[ ] )
{
	u32	ev = Trig_Event_Mask( "changes" );

	LocalName = av[ 0 ];
	if( ac < 5 ) {
		fprintf( stderr, "Usage: %s fork|poll directory log_file seconds\n", av[ 0 ] );
		exit( 1 );
	}
	if( av[ 1 ][ 0 ] == 'f' ) {
		pid_t	pid = Trig_Fork_Watch( av[ 2 ], ev, TRIG_F_RECURSIVE, 50, Callback, av[ 3 ] );
		int	st;

		if( pid <= 0 ) exit( 2 );
		printf( "watcher %d\n", pid );
		fflush( stdout );
		sleep( atoi( av[ 4 ] ) );
		st = Trig_Stop_Watch( pid );
		printf( "stopped status=%d\n", st );
	} else {
		trig_w_t *	w = Trig_Open( );
		u64		end = Trig_Mono( ) + 1000 * atoi( av[ 4 ] );

		if( w == NULL || Trig_Add( w, av[ 2 ], ev, TRIG_F_RECURSIVE, 50, 7 ) < 0 ) exit( 2 );
		printf( "polling\n" );
		fflush( stdout );
		while( Trig_Mono( ) < end ) Trig_Poll( w, 100, Callback, av[ 3 ] );
		Trig_Close( w );
		printf( "done\n" );
	}
	exit( 0 );
}
