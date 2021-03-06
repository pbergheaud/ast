/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1989-2011 AT&T Intellectual Property          *
*                      and is licensed under the                       *
*                 Eclipse Public License, Version 1.0                  *
*                    by AT&T Intellectual Property                     *
*                                                                      *
*                A copy of the License is available at                 *
*          http://www.eclipse.org/org/documents/epl-v10.html           *
*         (with md5 checksum b35adb5213ca9657e911e9befb180842)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*                 Glenn Fowler <gsf@research.att.com>                  *
*                  David Korn <dgk@research.att.com>                   *
*                   Eduardo Krell <ekrell@adexus.cl>                   *
*                                                                      *
***********************************************************************/
#pragma prototyped

#include "3d.h"

#ifdef	kill3d

int
kill3d(pid_t pid, int sig)
{
#if FS
	Mount_t*	mp;

#endif
	if (KILL(pid, sig))
		return(-1);
#if FS
	for (mp = state.global; mp; mp = mp->global)
		if (fssys(mp, MSG_kill))
			fscall(mp, MSG_kill, 0, pid, sig);
#endif
	return(0);
}

#else

NoN(kill)

#endif
