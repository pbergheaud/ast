/***********************************************************************
 *                                                                      *
 *               This software is part of the ast package               *
 *          Copyright (c) 1985-2011 AT&T Intellectual Property          *
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
/*
 * RE character class support
 */
#include "config_ast.h"  // IWYU pragma: keep

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "ast.h"
#include "reglib.h"

struct Ctype_s;
typedef struct Ctype_s Ctype_t;

struct Ctype_s {
    const char *name;
    size_t size;
    regclass_t ctype;
    Ctype_t *next;
    wctype_t wtype;
};

static Ctype_t *ctypes;

static int Isalnum(int c) { return iswalnum(c); }
static int Isalpha(int c) { return iswalpha(c); }
static int Isblank(int c) { return iswblank(c); }
static int Iscntrl(int c) { return iswcntrl(c); }
static int Isdigit(int c) { return iswdigit(c); }
static int Notdigit(int c) { return !iswdigit(c); }
static int Isgraph(int c) { return iswgraph(c); }
static int Islower(int c) { return iswlower(c); }
static int Isprint(int c) { return iswprint(c); }
static int Ispunct(int c) { return iswpunct(c); }
static int Isspace(int c) { return iswspace(c); }
static int Notspace(int c) { return !iswspace(c); }
static int Isupper(int c) { return iswupper(c); }
static int Isword(int c) { return iswalnum(c) || c == '_'; }
static int Notword(int c) { return !iswalnum(c) && c != '_'; }
static int Isxdigit(int c) { return iswxdigit(c); }

static int Is_wc_1(int);
static int Is_wc_2(int);
static int Is_wc_3(int);
static int Is_wc_4(int);
static int Is_wc_5(int);
static int Is_wc_6(int);
static int Is_wc_7(int);
static int Is_wc_8(int);
static int Is_wc_9(int);
static int Is_wc_10(int);
static int Is_wc_11(int);
static int Is_wc_12(int);
static int Is_wc_13(int);
static int Is_wc_14(int);
static int Is_wc_15(int);
static int Is_wc_16(int);

#define SZ(s) s, (sizeof(s) - 1)

static Ctype_t ctype[] = {
    {SZ("alnum"), Isalnum, NULL, 0},   {SZ("alpha"), Isalpha, NULL, 0},
    {SZ("blank"), Isblank, NULL, 0},   {SZ("cntrl"), Iscntrl, NULL, 0},
    {SZ("digit"), Isdigit, NULL, 0},   {SZ("graph"), Isgraph, NULL, 0},
    {SZ("lower"), Islower, NULL, 0},   {SZ("print"), Isprint, NULL, 0},
    {SZ("punct"), Ispunct, NULL, 0},   {SZ("space"), Isspace, NULL, 0},
    {SZ("upper"), Isupper, NULL, 0},   {SZ("word"), Isword, NULL, 0},
    {SZ("xdigit"), Isxdigit, NULL, 0},

#define CTYPES 13

    {NULL, 0, Is_wc_1, NULL, 0},       {NULL, 0, Is_wc_2, NULL, 0},
    {NULL, 0, Is_wc_3, NULL, 0},       {NULL, 0, Is_wc_4, NULL, 0},
    {NULL, 0, Is_wc_5, NULL, 0},       {NULL, 0, Is_wc_6, NULL, 0},
    {NULL, 0, Is_wc_7, NULL, 0},       {NULL, 0, Is_wc_8, NULL, 0},
    {NULL, 0, Is_wc_9, NULL, 0},       {NULL, 0, Is_wc_10, NULL, 0},
    {NULL, 0, Is_wc_11, NULL, 0},      {NULL, 0, Is_wc_12, NULL, 0},
    {NULL, 0, Is_wc_13, NULL, 0},      {NULL, 0, Is_wc_14, NULL, 0},
    {NULL, 0, Is_wc_15, NULL, 0},      {NULL, 0, Is_wc_16, NULL, 0},

};

static int Is_wc_1(int c) { return iswctype(c, ctype[CTYPES + 0].wtype); }
static int Is_wc_2(int c) { return iswctype(c, ctype[CTYPES + 1].wtype); }
static int Is_wc_3(int c) { return iswctype(c, ctype[CTYPES + 2].wtype); }
static int Is_wc_4(int c) { return iswctype(c, ctype[CTYPES + 3].wtype); }
static int Is_wc_5(int c) { return iswctype(c, ctype[CTYPES + 4].wtype); }
static int Is_wc_6(int c) { return iswctype(c, ctype[CTYPES + 5].wtype); }
static int Is_wc_7(int c) { return iswctype(c, ctype[CTYPES + 6].wtype); }
static int Is_wc_8(int c) { return iswctype(c, ctype[CTYPES + 7].wtype); }
static int Is_wc_9(int c) { return iswctype(c, ctype[CTYPES + 8].wtype); }
static int Is_wc_10(int c) { return iswctype(c, ctype[CTYPES + 9].wtype); }
static int Is_wc_11(int c) { return iswctype(c, ctype[CTYPES + 10].wtype); }
static int Is_wc_12(int c) { return iswctype(c, ctype[CTYPES + 11].wtype); }
static int Is_wc_13(int c) { return iswctype(c, ctype[CTYPES + 12].wtype); }
static int Is_wc_14(int c) { return iswctype(c, ctype[CTYPES + 13].wtype); }
static int Is_wc_15(int c) { return iswctype(c, ctype[CTYPES + 14].wtype); }
static int Is_wc_16(int c) { return iswctype(c, ctype[CTYPES + 15].wtype); }

/*
 * return pointer to ctype function for :class:] in s
 * s points to the first char after the initial [
 * dynamic wctype classes are locale-specific
 * dynamic entry locale is punned in Ctype_t.next
 * the search does a lazy (one entry at a time) flush on locale mismatch
 * if e!=0 it points to next char in s
 * 0 returned on error
 */

regclass_t regclass(const char *s, char **e) {
    Ctype_t *cp;
    int c;
    size_t n;
    const char *t;
    Ctype_t *lc;
    Ctype_t *xp;
    Ctype_t *zp;

    if (!(c = *s++)) return 0;
    for (t = s; *t && (*t != c || *(t + 1) != ']'); t++) {
        ;
    }
    if (*t != c || !(n = t - s)) return 0;
    for (cp = ctypes; cp; cp = cp->next) {
        if (n == cp->size && !strncmp(s, cp->name, n)) goto found;
    }
    xp = zp = NULL;
    lc = (Ctype_t *)ast_setlocale(LC_CTYPE, NULL);
    for (cp = ctype; cp < &ctype[elementsof(ctype)]; cp++) {
        if (!zp) {
            if (!cp->size) {
                zp = cp;
            } else if (!xp && cp->next && cp->next != lc) {
                xp = cp;
            }
        }
        if (n == cp->size && !strncmp(s, cp->name, n) && (!cp->next || cp->next == lc)) goto found;
    }
    if (!(cp = zp)) {
        if (!(cp = xp)) return 0;
        cp->size = 0;
        if (strcmp(cp->name, s) != 0) {
            free((void *)cp->name);  // discard const qualifier
            cp->name = NULL;
        }
    }
    if (!cp->name) {
        cp->name = memdup(s, n + 1);
        if (!cp->name) return 0;
        *((char *)cp->name + n) = 0;
    }
    /* mvs.390 needs the (char*) cast -- barf */
    if (!(cp->wtype = wctype((char *)cp->name))) {
        free((void *)cp->name);  // discard const qualifier
        cp->name = NULL;
        return 0;
    }
    cp->size = n;
    cp->next = lc;
found:
    if (e) *e = (char *)t + 2;
    return cp->ctype;
}

/*
 * return pointer to ctype function for token
 */

regclass_t classfun(int type) {
    switch (type) {
        case T_ALNUM:
            return Isword;
        case T_ALNUM_NOT:
            return Notword;
        case T_DIGIT:
            return Isdigit;
        case T_DIGIT_NOT:
            return Notdigit;
        case T_SPACE:
            return Isspace;
        case T_SPACE_NOT:
            return Notspace;
    }
    return 0;
}
