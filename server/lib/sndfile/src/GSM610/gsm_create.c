/*
 * Copyright 1992 by Jutta Degener and Carsten Bormann, Technische
 * Universitaet Berlin.  See the accompanying file "COPYRIGHT" for
 * details.  THERE IS ABSOLUTELY NO WARRANTY FOR THIS SOFTWARE.
 */

static char const	ident[] = "$Header: /cvsroot/mpeg4ip/mpeg4ip/server/lib/sndfile/src/GSM610/Attic/gsm_create.c,v 1.2 2001/05/09 21:04:38 cahighlander Exp $";

#include	"config.h"

#ifdef	HAS_STRING_H
#include	<string.h>
#else
#	include "proto.h"
	extern char	* memset P((char *, int, int));
#endif

#ifdef	HAS_STDLIB_H
#	include	<stdlib.h>
#else
#	ifdef	HAS_MALLOC_H
#		include 	<malloc.h>
#	else
		extern char * malloc();
#	endif
#endif

#include <stdio.h>

#include "gsm.h"
#include "private.h"
#include "proto.h"

gsm gsm_create P0()
{
	gsm  r;

	r = (gsm)malloc(sizeof(struct gsm_state));
	if (!r) return r;

	memset((char *)r, 0, sizeof(*r));
	r->nrp = 40;

	return r;
}
