/***********************************************************************
 *                                                                      *
 *               This software is part of the ast package               *
 *          Copyright (c) 1985-2013 AT&T Intellectual Property          *
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
 *               Glenn Fowler <glenn.s.fowler@gmail.com>                *
 *                    David Korn <dgkorn@gmail.com>                     *
 *                     Phong Vo <phongvo@gmail.com>                     *
 *                                                                      *
 ***********************************************************************/
#include "config_ast.h"  // IWYU pragma: keep

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sfhdr.h"
#include "sfio.h"
#include "vthread.h"

#include "ast.h"

/*	Functions to set a given stream to some desired mode
**
**	Written by Kiem-Phong Vo.
**
**	Modifications:
**		06/27/1990 (first version)
**		01/06/1991
**		07/08/1991
**		06/18/1992
**		02/02/1993
**		05/25/1993
**		02/07/1994
**		05/21/1996
**		08/01/1997
**		08/01/1998 (extended formatting)
**		09/09/1999 (thread-safe)
**		02/01/2001 (adaptive buffering)
**		05/31/2002 (multi-byte handling in sfvprintf/vscanf)
**		09/06/2002 (SF_IOINTR flag)
**		11/15/2002 (%#c for sfvprintf)
**		05/31/2003 (sfsetbuf(f,f,align_size) to set alignment for data)
**			   (%I1d is fixed to handle "signed char" correctly)
**		01/01/2004 Porting issues to various platforms resolved.
**		06/01/2008 Allowing notify() at entering/exiting thread-safe routines.
**		09/15/2008 Add sfwalk().
*/

/* the below is for protecting the application from SIGPIPE */
#include <signal.h>
#include <sys/wait.h>

#include "sig.h"

static int _Sfsigp = 0; /* # of streams needing SIGPIPE protection */

/* done at exiting time */
static_fn void _sfcleanup(void) {
    Sfpool_t *p;
    int n;
    int pool;

    // Set this so that no more buffering is allowed for write streams.
    _Sfexiting = 1001;

    sfsync(NULL);

    for (p = &_Sfpool; p; p = p->next) {
        for (n = 0; n < p->n_sf; ++n) {
            Sfio_t *f = p->sf[n];
            if (!f || SFFROZEN(f)) continue;

            SFLOCK(f, 0)
            SFMTXLOCK(f)

            /* let application know that we are leaving */
            (void)SFRAISE(f, SF_ATEXIT, NULL);

            if (f->flags & SF_STRING) continue;

            /* from now on, write streams are unbuffered */
            pool = f->mode & SF_POOL;
            f->mode &= ~SF_POOL;
            if ((f->flags & SF_WRITE) && !(f->mode & SF_WRITE)) (void)_sfmode(f, SF_WRITE, 1);
            if (f->data && ((f->bits & SF_MMAP) || ((f->mode & SF_WRITE) && f->next == f->data))) {
                (void)SFSETBUF(f, NULL, 0);
            }
            f->mode |= pool;

            SFMTXUNLOCK(f)
            SFOPEN(f)
        }
    }
}

/* put into discrete pool */
int _sfsetpool(Sfio_t *f) {
    Sfpool_t *p;
    Sfio_t **array;
    int n, rv;

    if (!_Sfcleanup) {
        _Sfcleanup = _sfcleanup;
        (void)atexit(_sfcleanup);
    }

    if (!(p = f->pool)) p = f->pool = &_Sfpool;

    POOLMTXENTER(p);

    rv = -1;

    if (p->n_sf >= p->s_sf) {
        if (p->s_sf == 0) /* initialize pool array */
        {
            p->s_sf = sizeof(p->array) / sizeof(p->array[0]);
            p->sf = p->array;
        } else /* allocate a larger array */
        {
            n = (p->sf != p->array ? p->s_sf : (p->s_sf / 4 + 1) * 4) + 4;
            if (!(array = malloc(n * sizeof(Sfio_t *)))) goto done;

            /* move old array to new one */
            memcpy((void *)array, (void *)p->sf, p->n_sf * sizeof(Sfio_t *));
            if (p->sf != p->array) free(p->sf);

            p->sf = array;
            p->s_sf = n;
        }
    }

    /* always add at end of array because if this was done during some sort
       of walk thru all streams, we'll want the new stream to be seen.
    */
    p->sf[p->n_sf++] = f;
    rv = 0;

done:
    POOLMTXRETURN(p, rv);
}

/* create an auxiliary buffer for sfgetr/sfreserve/sfputr */
Sfrsrv_t *_sfrsrv(Sfio_t *f, ssize_t size) {
    Sfrsrv_t *rsrv, *rs;

    /* make buffer if nothing yet */
    size = ((size + SF_GRAIN - 1) / SF_GRAIN) * SF_GRAIN;
    if (!(rsrv = f->rsrv) || size > rsrv->size) {
        rs = malloc(size + sizeof(Sfrsrv_t));
        if (!rs) {
            size = -1;
        } else {
            if (rsrv) {
                if (rsrv->slen > 0) memcpy(rs, rsrv, sizeof(Sfrsrv_t) + rsrv->slen);
                free(rsrv);
            }
            f->rsrv = rsrv = rs;
            rsrv->size = size;
            rsrv->slen = 0;
        }
    }

    if (rsrv && size > 0) rsrv->slen = 0;

    return size >= 0 ? rsrv : NULL;
}

#ifdef SIGPIPE
static_fn void ignoresig(int sig) { signal(sig, ignoresig); }
#endif

int _sfpopen(Sfio_t *f, int fd, int pid, int stdio) {
    Sfproc_t *p;

    if (f->proc) return 0;

    p = f->proc = malloc(sizeof(Sfproc_t));
    if (!p) return -1;

    p->pid = pid;
    p->size = p->ndata = 0;
    p->rdata = NULL;
    p->file = fd;
    p->sigp = (!stdio && pid >= 0 && (f->flags & SF_WRITE)) ? 1 : 0;

#ifdef SIGPIPE /* protect from broken pipe signal */
    if (p->sigp) {
        sig_t handler;

        (void)vtmtxlock(_Sfmutex);
        if ((handler = signal(SIGPIPE, ignoresig)) != SIG_DFL && handler != ignoresig) {
            signal(SIGPIPE, handler); /* honor user handler */
        }
        _Sfsigp += 1;
        (void)vtmtxunlock(_Sfmutex);
    }
#endif

    return 0;
}

int _sfpclose(Sfio_t *f) {
    Sfproc_t *p;
    int status;

    if (!(p = f->proc)) return -1;
    f->proc = NULL;

    if (p->rdata) free(p->rdata);

    if (p->pid < 0) {
        status = 0;
    } else { /* close the associated stream */
        if (p->file >= 0) CLOSE(p->file);

        /* wait for process termination */
        sigcritical(SIG_REG_EXEC | SIG_REG_PROC);
        status = -1;
        while (waitpid(p->pid, &status, 0) == -1 && errno == EINTR) {
            ;
        }
        status = status == -1 ? EXIT_QUIT
                              : WIFSIGNALED(status) ? EXIT_TERM(WTERMSIG(status))
                                                    : EXIT_CODE(WEXITSTATUS(status));
        sigcritical(SIG_REG_POP);

#ifdef SIGPIPE
        (void)vtmtxlock(_Sfmutex);
        if (p->sigp && (_Sfsigp -= 1) <= 0) {
            sig_t handler;
            if ((handler = signal(SIGPIPE, SIG_DFL)) != SIG_DFL && handler != ignoresig) {
                signal(SIGPIPE, handler); /* honor user handler */
            }
            _Sfsigp = 0;
        }
        (void)vtmtxunlock(_Sfmutex);
#endif
    }

    free(p);
    return status;
}

static_fn int _sfpmode(Sfio_t *f, int type) {
    Sfproc_t *p;

    if (!(p = f->proc)) return -1;

    if (type == SF_WRITE) { /* save unread data */
        p->ndata = f->endb - f->next;
        if (p->ndata > p->size) {
            if (p->rdata) free(p->rdata);
            p->rdata = malloc(p->ndata);
            if (p->rdata) {
                p->size = p->ndata;
            } else {
                p->size = 0;
                return -1;
            }
        }
        if (p->ndata > 0) memcpy((void *)p->rdata, (void *)f->next, p->ndata);
        f->endb = f->data;
    } else {                      /* restore read data */
        if (p->ndata > f->size) { /* may lose data!!! */
            p->ndata = f->size;
        }
        if (p->ndata > 0) {
            memcpy((void *)f->data, (void *)p->rdata, p->ndata);
            f->endb = f->data + p->ndata;
            p->ndata = 0;
        }
    }

    /* switch file descriptor */
    if (p->pid >= 0) {
        type = f->file;
        f->file = p->file;
        p->file = type;
    }

    return 0;
}

int _sfmode(Sfio_t *f, int wanted, int local) {
    int n;
    Sfoff_t addr;
    int rv = 0;

    SFONCE()  // initialize mutexes

    if (wanted & SF_SYNCED) /* for (SF_SYNCED|SF_READ) stream, just junk data */
    {
        wanted &= ~SF_SYNCED;
        if ((f->mode & (SF_SYNCED | SF_READ)) == (SF_SYNCED | SF_READ)) {
            f->next = f->endb = f->endr = f->data;
            f->mode &= ~SF_SYNCED;
        }
    }

    if ((!local && SFFROZEN(f)) || (!(f->flags & SF_STRING) && f->file < 0)) {
        if (local || !f->disc || !f->disc->exceptf) {
            local = 1;
            goto err_notify;
        }

        for (;;) {
            if ((rv = (*f->disc->exceptf)(f, SF_LOCKED, 0, f->disc)) < 0) return rv;
            if ((!local && SFFROZEN(f)) || (!(f->flags & SF_STRING) && f->file < 0)) {
                if (rv == 0) {
                    local = 1;
                    goto err_notify;
                } else {
                    continue;
                }
            } else {
                break;
            }
        }
    }

    if (f->mode & SF_GETR) {
        f->mode &= ~SF_GETR;
        if (f->bits & SF_MMAP) {
            if (!++f->ngetr) f->tiny[0]++;
            if (((f->tiny[0] << 8) | f->ngetr) >=
                (4 * SF_NMAP)) { /* turn off mmap to avoid page faulting */
                sfsetbuf(f, (void *)f->tiny, (size_t)SF_UNBOUND);
                f->ngetr = f->tiny[0] = 0;
            }
        }
        if (f->getr) {
            f->next[-1] = f->getr;
            f->getr = 0;
        }
    }

    if (f->mode & SF_STDIO) { /* synchronizing with stdio pointers */
        (*_Sfstdsync)(f);
    }

    if (f->disc == _Sfudisc && wanted == SF_WRITE && sfclose((*_Sfstack)(f, NULL)) < 0) {
        local = 1;
        goto err_notify;
    }

    if (f->mode & SF_POOL) { /* move to head of pool */
        if (f == f->pool->sf[0] || (*_Sfpmove)(f, 0) < 0) {
            local = 1;
            goto err_notify;
        }
        f->mode &= ~SF_POOL;
    }

    SFLOCK(f, local)

    /* buffer initialization */
    wanted &= SF_RDWR;
    if (f->mode & SF_INIT) {
        if (!f->pool && _sfsetpool(f) < 0) {
            rv = -1;
            goto done;
        }

        if (wanted == 0) goto done;

        if (wanted != (int)(f->mode & SF_RDWR) && !(f->flags & wanted)) goto err_notify;

        if ((f->flags & SF_STRING) && f->size >= 0 && f->data) {
            f->mode &= ~SF_INIT;
            f->extent = ((f->flags & SF_READ) || (f->bits & SF_BOTH)) ? f->size : 0;
            f->here = 0;
            f->endb = f->data + f->size;
            f->next = f->endr = f->endw = f->data;
            if (f->mode & SF_READ) {
                f->endr = f->endb;
            } else {
                f->endw = f->endb;
            }
        } else {
            n = f->flags;
            (void)SFSETBUF(f, f->data, f->size);
            f->flags |= (n & SF_MALLOC);
        }
    }

    if (wanted == (int)SFMODE(f, 1)) goto done;

    switch (SFMODE(f, 1)) {
        case SF_WRITE: /* switching to SF_READ */
            if (wanted == 0 || wanted == SF_WRITE) break;
            if (!(f->flags & SF_READ)) {
                goto err_notify;
            } else if (f->flags & SF_STRING) {
                SFSTRSIZE(f);
                f->endb = f->data + f->extent;
                f->mode = SF_READ;
                break;
            }

            /* reset buffer */
            if (f->next > f->data && SFFLSBUF(f, -1) < 0) goto err_notify;

            if (f->size == 0) { /* unbuffered */
                f->data = f->tiny;
                f->size = sizeof(f->tiny);
            }
            f->next = f->endr = f->endw = f->endb = f->data;
            f->mode = SF_READ | SF_LOCK;

            /* restore saved read data for coprocess */
            if (f->proc && _sfpmode(f, wanted) < 0) goto err_notify;

            break;

        case (SF_READ | SF_SYNCED):   /* a previously sync-ed read stream */
            if (wanted != SF_WRITE) { /* just reset the pointers */
                f->mode = SF_READ | SF_LOCK;

                /* see if must go with new physical location */
                if ((f->flags & (SF_SHARE | SF_PUBLIC)) == (SF_SHARE | SF_PUBLIC) &&
                    (addr = SFSK(f, 0, SEEK_CUR, f->disc)) != f->here) {
                    if ((f->bits & SF_MMAP) && f->data) {
                        SFMUNMAP(f, f->data, f->endb - f->data);
                        f->data = NULL;
                    }
                    f->endb = f->endr = f->endw = f->next = f->data;
                    f->here = addr;
                } else {
                    addr = f->here + (f->endb - f->next);
                    if (SFSK(f, addr, SEEK_SET, f->disc) < 0) goto err_notify;
                    f->here = addr;
                }

                break;
            }
            /* fall thru */

        case SF_READ: /* switching to SF_WRITE */
            if (wanted != SF_WRITE) {
                break;
            } else if (!(f->flags & SF_WRITE)) {
                goto err_notify;
            } else if (f->flags & SF_STRING) {
                f->endb = f->data + f->size;
                f->mode = SF_WRITE | SF_LOCK;
                break;
            }

            /* save unread data before switching mode */
            if (f->proc && _sfpmode(f, wanted) < 0) goto err_notify;

            /* reset buffer and seek pointer */
            if (!(f->mode & SF_SYNCED)) {
                n = f->endb - f->next;
                if (f->extent >= 0 &&
                    (n > 0 || (f->data && (f->bits & SF_MMAP)))) { /* reset file pointer */
                    addr = f->here - n;
                    if (SFSK(f, addr, SEEK_SET, f->disc) < 0) goto err_notify;
                    f->here = addr;
                }
            }

            f->mode = SF_WRITE | SF_LOCK;
            if (f->bits & SF_MMAP) {
                if (f->data) SFMUNMAP(f, f->data, f->endb - f->data);
                (void)SFSETBUF(f, (void *)f->tiny, (size_t)SF_UNBOUND);
            }
            if (f->data == f->tiny) {
                f->endb = f->data = f->next = NULL;
                f->size = 0;
            } else {
                f->endb = (f->next = f->data) + f->size;
            }

            break;

        default: /* unknown case */
        err_notify:
            if ((wanted &= SF_RDWR) == 0 && (wanted = f->flags & SF_RDWR) == SF_RDWR) {
                wanted = SF_READ;
            }

            /* set errno for operations that access wrong stream type */
            if (wanted != (f->mode & SF_RDWR) && f->file >= 0) errno = EBADF;

            if (_Sfnotify) { /* notify application of the error */
                (*_Sfnotify)(f, wanted, (void *)((long)f->file));
            }

            rv = -1;
            break;
    }

done:
    if (!local) SFOPEN(f)
    return rv;
}
