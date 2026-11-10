/* SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later */
/* Copyright (c) 2026 John R. Smythe Jr. */
/*******************************************************************************
 * 
 *	Trigger Event Name Functions
 *		Trig_Event_Mask( )
 *		Trig_Event_Names( )
 ******************************************************************************/
#include	"triglocal.h"

static	struct	ev_s	{
	char	*	name;
	u32		mask;
}	evnts[ ] =	{
	{ "changes",	IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO },
	{ "all",	IN_ALL_EVENTS & ~( IN_ACCESS | IN_OPEN | IN_CLOSE_NOWRITE ) },
	{ "move",	IN_MOVED_FROM | IN_MOVED_TO },
	{ "self",	IN_DELETE_SELF | IN_MOVE_SELF },
	{ "create",	IN_CREATE },
	{ "write",	IN_CLOSE_WRITE },
	{ "modify",	IN_MODIFY },
	{ "delete",	IN_DELETE },
	{ "moved_from",	IN_MOVED_FROM },
	{ "moved_to",	IN_MOVED_TO },
	{ "attrib",	IN_ATTRIB },
	{ "access",	IN_ACCESS },
	{ "open",	IN_OPEN },
	{ "close_nowrite", IN_CLOSE_NOWRITE },
	{ "delete_self", IN_DELETE_SELF },
	{ "move_self",	IN_MOVE_SELF },
	{ "unmount",	IN_UNMOUNT },
	{ "overflow",	IN_Q_OVERFLOW },
	{ "ignored",	IN_IGNORED },
	{ "isdir",	IN_ISDIR },
	{ NULL,		0 }
};

/*
 *	"create,write,0x200" -> Mask, 0 On Unknown Name
 */

u32	Trig_Event_Mask( char * names )
{
	u32	mask = 0;
	char	buf[ 256 ];
	char *	sp;
	char *	tok;

	snprintf( buf, sizeof( buf ), "%s", names );
	for( tok = strtok_r( buf, ",|+ ", &sp ); tok != NULL; tok = strtok_r( NULL, ",|+ ", &sp ) ) {
		struct	ev_s *	ev;

		if( tok[ 0 ] >= '0' && tok[ 0 ] <= '9' ) {
			mask |= (u32)strtoul( tok, NULL, 0 );
			continue;
		}
		for( ev = evnts; ev->name != NULL; ev++ ) {
			if( strcasecmp( tok, ev->name ) == 0 ) break;
		}
		if( ev->name == NULL ) return( 0 );
		mask |= ev->mask;
	}
	return( mask );
}

char *	Trig_Event_Names( u32 mask, char * buf, int size )
{
	int	l = 0;

	buf[ 0 ] = 0;
	for( struct ev_s * ev = &evnts[ 4 ]; ev->name != NULL && l < size; ev++ ) {
		if( ( mask & ev->mask ) == 0 ) continue;
		l += snprintf( &buf[ l ], size - l, "%s%s", l ? "," : "", ev->name );
		mask &= ~ev->mask;
	}
	if( mask != 0 && l < size ) snprintf( &buf[ l ], size - l, "%s0x%X", l ? "," : "", mask );
	return( buf );
}
