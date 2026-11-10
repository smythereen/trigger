/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Shared Memory (.trigmem) File Functions
 *		Trig_Mem_Create( )
 *		Trig_Mem_Map( )
 *		Trig_Mem_Grow( )
 *		Trig_Mem_Unmap( )
 *		Trig_Time( )
 *		Trig_Mono( )
 ******************************************************************************/
#include	"triglocal.h"

u64	Trig_Time( void )
{
	struct	timespec	ts;

	clock_gettime( CLOCK_REALTIME, &ts );
	return( (u64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000 );
}

u64	Trig_Mono( void )
{
	struct	timespec	ts;

	clock_gettime( CLOCK_MONOTONIC, &ts );
	return( (u64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 );
}

int	Trig_Mem_Create( char * file, u32 max, u32 debounce_ms )
{
	trig_hdr_t	h;
	int		fd;

	if( max == 0 ) max = TRIG_DEF_MAX;
	if( ( fd = open( file, O_RDWR | O_CREAT | O_EXCL, 0666 ) ) == -1 ) {
		Trace( 0, "Trig_Mem_Create: open( %s ) errno=%d - %s", file, errno, strerror( errno ) );
		return( -1 );
	}
	memset( &h, 0, sizeof( h ) );
	h.magic = TRIG_MAGIC;
	h.version = TRIG_VERSION;
	h.hdr_size = sizeof( trig_hdr_t );
	h.ent_size = sizeof( trig_ent_t );
	h.max = max;
	h.debounce_ms = debounce_ms;
	if( ftruncate( fd, TRIG_FILE_SIZE( max ) ) == -1 ||
	    write( fd, &h, sizeof( h ) ) != sizeof( h ) ) {
		Trace( 0, "Trig_Mem_Create: %s errno=%d - %s", file, errno, strerror( errno ) );
		close( fd );
		unlink( file );
		return( -1 );
	}
	close( fd );
	Trace( TRIG_TRC_LVL, "Trig_Mem_Create: %s max=%u", file, max );
	return( 0 );
}

trig_hdr_t *	Trig_Mem_Map( char * file, u64 * size )
{
	trig_hdr_t *	h;
	struct	stat	st;
	int		fd;

	if( ( fd = open( file, O_RDWR ) ) == -1 ) {
		Trace( 0, "Trig_Mem_Map: open( %s ) errno=%d - %s", file, errno, strerror( errno ) );
		return( NULL );
	}
	if( fstat( fd, &st ) == -1 || st.st_size < (off_t)sizeof( trig_hdr_t ) ) {
		Trace( 0, "Trig_Mem_Map: %s is too small", file );
		close( fd );
		return( NULL );
	}
	h = mmap( NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
	close( fd );
	if( h == MAP_FAILED ) {
		Trace( 0, "Trig_Mem_Map: mmap( %s ) errno=%d - %s", file, errno, strerror( errno ) );
		return( NULL );
	}
	if( h->magic != TRIG_MAGIC || h->version != TRIG_VERSION ||
	    h->hdr_size != sizeof( trig_hdr_t ) || h->ent_size != sizeof( trig_ent_t ) ||
	    (u64)st.st_size < TRIG_FILE_SIZE( h->max ) ) {
		Trace( 0, "Trig_Mem_Map: %s is not a version %u .trigmem file", file, TRIG_VERSION );
		munmap( h, st.st_size );
		return( NULL );
	}
	*size = st.st_size;
	Trace( TRIG_TRC_LVL, "Trig_Mem_Map: %s size=%" PRIu64 " max=%u count=%u", file, *size, h->max, h->count );
	return( h );
}

int	Trig_Mem_Grow( char * file, trig_hdr_t ** hp, u64 * size, u32 max )
{
	if( max <= (*hp)->max ) return( 0 );
	if( truncate( file, TRIG_FILE_SIZE( max ) ) == -1 ) return( -1 );
	(*hp)->max = max;
	Trig_Mem_Unmap( *hp, *size );
	return( ( *hp = Trig_Mem_Map( file, size ) ) == NULL ? -1 : 0 );
}

void	Trig_Mem_Unmap( trig_hdr_t * h, u64 size )
{
	if( h != NULL ) munmap( h, size );
}
