/*
 * pmstore [-h hostname ] [-i inst[,inst...]] [-n pmnsfile ] metric value
 *
 *
 * Copyright (c) 1995,2004 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 * 
 * Contact information: Silicon Graphics, Inc., 1500 Crittenden Lane,
 * Mountain View, CA 94043, USA, or: http://www.sgi.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "pmapi.h"
#include "impl.h"
#include "pmda.h"
#ifdef HAVE_VALUES_H
#include <values.h>
#endif
#include <float.h>

extern int	errno;

#define	IS_UNKNOWN	15
#define	IS_STRING	1
#define IS_INTEGER	2
#define IS_FLOAT	4
#define IS_HEX		8

#ifndef HAVE_STRTOLL
/*
 * cheap hack ...won't work for large values!
 */
static __int64_t
strtoll(char *p, char **endp, int base)
{
    return (__int64_t)strtol(p, endp, base);
}
#endif

#ifndef HAVE_STRTOULL
/*
 * cheap hack ...won't work for large values!
 */
static __uint64_t
strtoull(char *p, char **endp, int base)
{
    return (__uint64_t)strtoul(p, endp, base);
}
#endif



static void
mkAtom(pmAtomValue *avp, int *nbyte, int type, char *buf)
{
    char	*p = buf;
    char	*endbuf;
    int		vtype = IS_UNKNOWN;
    int		seendot = 0;
    int		base;
    double	d;
    void	*vp;
    long	temp_l;
    unsigned long	temp_ul;
	

    /*
     * for strtol() et al, start with optional white space, then
     * optional sign, then optional hex prefix, then stuff ...
     */
    p = buf;
    while (*p && isspace((int)*p)) p++;
    if (*p && (*p == '-' || *p == '-')) p++;

    if (*p && *p == '0' && p[1] && tolower(p[1]) == 'x') {
	p += 2;
    }
    else {
	vtype &= ~IS_HEX; /* hex MUST start with 0x or 0X */
    }

    /*
     * does it smell like a hex number or a floating point number?
     */
    while (*p) {
	if (!isdigit((int)*p)) {
	    vtype &= ~IS_INTEGER;
	    if (!isxdigit((int)*p) ) {
		vtype &= ~IS_HEX;
		if (*p == '.')
		    seendot++;
	    }
	}
	p++;
    }

    if (seendot != 1)
	/* more or less than one '.' and it is not a floating point number */
	vtype &= ~IS_FLOAT;

    endbuf = buf;
    base = (vtype & IS_HEX) ? 16:10;

    switch (type) {
	case PM_TYPE_32:
		temp_l = strtol(buf, &endbuf, base);
		if (errno != ERANGE) {
		    /*
		     * ugliness here is for cases where pmstore is compiled
		     * 64-bit (e.g. on ia64) and then strtol() may return
		     * values larger than 32-bits with no error indication
		     * ... if this is being compiled 32-bit, then the
		     * condition will be universally false, and a smart
		     * compiler may notice and warn.
		     */
#ifdef HAVE_64BIT_LONG
		    if (temp_l > 0x7fffffffLL || temp_l < (-0x7fffffffLL - 1))
			errno = ERANGE;
		    else 
#endif
		    {
			avp->l = (__int32_t)temp_l;
			*nbyte = sizeof(avp->l);
		    }
		}
		break;

	case PM_TYPE_U32:
		temp_ul = strtoul(buf, &endbuf, base);
		if (errno != ERANGE) {
#ifdef HAVE_64BIT_LONG
		    if (temp_ul > 0xffffffffLL)
			errno = ERANGE;
		    else 
#endif
		    {
			avp->ul = (__uint32_t)temp_ul;
			*nbyte = sizeof(avp->ul);
		    }
		}
		break;

	case PM_TYPE_64:
		avp->ll = strtoll(buf, &endbuf, base);
		/* trust the library to set errno to ERANGE as appropriate * */
		*nbyte = sizeof(avp->ll);
		break;

	case PM_TYPE_U64:
		/* trust the library to set errno to ERANGE as appropriate * */
		avp->ull = strtoull(buf, &endbuf, base);
		*nbyte = sizeof(avp->ull);
		break;

	case PM_TYPE_FLOAT:
		if ( vtype & IS_HEX ) {
		    /* strtod from GNU libc would try to convert it using some
		     * strange algo - we don't want it */
		    endbuf = buf;
		}
		else {
		    d = strtod(buf, &endbuf);
		    if (d < FLT_MIN || d > FLT_MAX)
			errno = ERANGE;
		    else {
			avp->f = (float)d;
			*nbyte = sizeof(avp->f);
		    }
		}
		break;

	case PM_TYPE_DOUBLE:
		if ( vtype & IS_HEX ) {
		    /* strtod from GNU libc would try to convert it using some
		     * strange algo - we don't want it */
		    endbuf = buf;
		}
		else {
		    avp->d = strtod(buf, &endbuf);
		    *nbyte = sizeof(avp->d);
		}
		break;

	case PM_TYPE_STRING:
		*nbyte = strlen(buf) + 1;
		if ((avp->cp = strdup (buf)) == NULL) {
		    __pmNoMem("pmstore", *nbyte, PM_FATAL_ERR);
		}
		endbuf = "";
		break;

	case PM_TYPE_AGGREGATE:
		/* assume the worst, and scale back later */
		if ((vtype & IS_HEX) || (vtype & IS_INTEGER))
		    *nbyte = sizeof(__int64_t);
		else if (vtype & IS_FLOAT)
		    *nbyte = sizeof(double);
		else
		    *nbyte = strlen(buf);
		vp = (void *)malloc(*nbyte);
		if (vp == NULL) {
		    __pmNoMem("pmstore", *nbyte, PM_FATAL_ERR);
		}
		if (vtype & (IS_HEX | IS_INTEGER) ) {
		    /* Try short one first */
		    temp_ul = strtoul(buf, &endbuf, base);
		    if (errno == ERANGE ||
			(sizeof(temp_ul) > sizeof(avp->ul) && (temp_ul & 0xffffffff00000000LL) != 0)) {
			errno = 0;
			avp->ull = strtoull(buf, &endbuf, base);
			if (errno != ERANGE) {
			    *nbyte = sizeof(avp->ull);
			    memcpy(vp, &avp->ull, *nbyte);
			}
		    }
		    else {
			avp->ul = (__uint32_t)temp_ul;
			*nbyte = sizeof(avp->ul);
			memcpy(vp, &avp->ul, *nbyte);
		    }
		}
		else if (vtype & IS_FLOAT) {
		    d = strtod(buf, &endbuf);
		    if (errno != ERANGE) {
			if (FLT_MIN <= d && d <= FLT_MAX) {
			    *nbyte = sizeof(float);
			    avp->f = (float)d;
			    memcpy(vp, &avp->f, *nbyte);
			}
			else
			    memcpy(vp, &d, *nbyte);
		    }
		}
		else
		    strncpy(vp, buf, *nbyte);

		avp->vp = vp;
		endbuf = "";
		break;
    }
    if (*endbuf != '\0') {
	fprintf(stderr,
			"The value \"%s\" is incompatible with the data "
			"type (PM_TYPE_%s)\n",
			buf, pmTypeStr(type));
	exit(1);
    }
    if (errno == ERANGE) {
	fprintf(stderr, 
			"The value \"%s\" is out of range for the data "
			"type (PM_TYPE_%s)\n",
			buf, pmTypeStr(type));
	exit(1);
    }
}

int
main(argc, argv)
int argc;
char *argv[];
{
    int		sts;
    int		n;
    int		c;
    int		i;
    int		j;
    char	*p;
    int		type = 0;
    char	*host;
    char	local[MAXHOSTNAMELEN];
    char	*pmnsfile = PM_NS_DEFAULT;
    int		errflag = 0;
    char	*namelist[1];
    pmID	pmidlist[1];
    pmResult	*result;
    char	**instnames = NULL;
    int		numinst = 0;
    pmDesc	desc;
    pmAtomValue	nav;
    int		aggr_len;
    pmValueSet	*vsp;
    char        *subopt;
    extern char	*optarg;
    extern int	optind;
    extern int	pmDebug;

    /* trim command name of leading directory components */
    pmProgname = argv[0];
    for (p = pmProgname; *p; p++) {
	if (*p == '/')
	    pmProgname = p+1;
    }

#ifdef HAVE_GETOPT_NEEDS_POSIXLY_CORRECT
    /*
     * without this, "pmstore some.metric -1234" fails because
     * -1234 is interpreted as command line options.
     */
    putenv("POSIXLY_CORRECT=");
#endif
    while ((c = getopt(argc, argv, "D:h:i:n:?")) != EOF) {
	switch (c) {

	case 'D':	/* debug flag */
	    sts = __pmParseDebug(optarg);
	    if (sts < 0) {
		fprintf(stderr, "%s: unrecognized debug flag specification (%s)\n",
		    pmProgname, optarg);
		errflag++;
	    }
	    else
		pmDebug |= sts;
	    break;

	case 'h':	/* contact PMCD on this hostname */
	    if (type != 0) {
		fprintf(stderr, "%s: at most one of -a and/or -h allowed\n", pmProgname);
		errflag++;
	    }
	    host = optarg;
	    type = PM_CONTEXT_HOST;
	    break;

	case 'i':	/* list of instances */
#define WHITESPACE ", \t\n"
	    subopt = strtok(optarg, WHITESPACE);
	    while (subopt != NULL) {
		numinst++;
		instnames =
		    (char **)realloc(instnames, numinst * sizeof(char *));
		if (instnames == NULL) {
		    __pmNoMem("pmstore.instnames", numinst * sizeof(char *), PM_FATAL_ERR);
		}
	       instnames[numinst-1] = subopt;
	       subopt = strtok(NULL, WHITESPACE);
	    }
	    break;

	case 'n':	/* alternative namespace file */
	    pmnsfile = optarg;
	    break;

	case '?':
	default:
	    errflag++;
	    break;
	}
    }

    if (errflag || optind != argc - 2) {
	fprintf(stderr,
"Usage: %s [options] metricname value\n"
"\n"
"Options:\n"
"  -h host       metrics source is PMCD on host\n"
"  -i instance   metric instance or list of instances. Elements in an\n"
"                instance list are separated by commas and/or newlines\n" 
"  -n pmnsfile   use an alternative PMNS\n",
			pmProgname);
	exit(1);
    }

    if (pmnsfile != PM_NS_DEFAULT) {
	if ((n = pmLoadNameSpace(pmnsfile)) < 0) {
	    fprintf(stderr, "pmLoadNameSpace: %s\n", pmErrStr(n));
	    exit(1);
	}
    }

    if (type == 0) {
	type = PM_CONTEXT_HOST;
	(void)gethostname(local, MAXHOSTNAMELEN);
	local[MAXHOSTNAMELEN-1] = '\0';
	host = local;
    }
    if ((sts = pmNewContext(type, host)) < 0) {
	if (type == PM_CONTEXT_HOST)
	    fprintf(stderr, "%s: Cannot connect to PMCD on host \"%s\": %s\n",
		pmProgname, host, pmErrStr(sts));
	else
	    fprintf(stderr, "%s: Cannot open archive \"%s\": %s\n",
		pmProgname, host, pmErrStr(sts));
	exit(1);
    }

    namelist[0] = argv[optind++];
    if ((n = pmLookupName(1, namelist, pmidlist)) < 0) {
	printf("%s: pmLookupName: %s\n", namelist[0], pmErrStr(n));
	exit(1);
    }
    if (pmidlist[0] == PM_ID_NULL) {
	printf("%s: unknown metric\n", namelist[0]);
	exit(1);
    }
    if ((n = pmLookupDesc(pmidlist[0], &desc)) < 0) {
	printf("%s: pmLookupDesc: %s\n", namelist[0], pmErrStr(n));
	exit(1);
    }
    if (instnames != NULL) {
	pmDelProfile(desc.indom, 0, NULL);
	for (i = 0; i < numinst; i++) {
	    if ((n = pmLookupInDom(desc.indom, instnames[i])) < 0) {
		printf("pmLookupInDom %s[%s]: %s\n",
		    namelist[0], instnames[i], pmErrStr(n));
		exit(1);
	    }
	    if ((sts = pmAddProfile(desc.indom, 1, &n)) < 0) {
		printf("pmAddProfile %s[%s]: %s\n",
		    namelist[0], instnames[i], pmErrStr(sts));
		exit(1);
	    }
	}
    }
    if ((n = pmFetch(1, pmidlist, &result)) < 0) {
	printf("%s: pmFetch: %s\n", namelist[0], pmErrStr(n));
	exit(1);
    }

    /* value is argv[optind] */
    mkAtom(&nav, &aggr_len, desc.type, argv[optind]);

    vsp = result->vset[0];
    if (vsp->numval == 0) {
	printf("%s: No value(s) available!\n", namelist[0]);
	exit(1);
    }
    else if (vsp->numval < 0) {
	printf("%s: Error: %s\n", namelist[0], pmErrStr(vsp->numval));
	exit(1);
    }
    for (j = 0; j < vsp->numval; j++) {
	pmValue	*vp = &vsp->vlist[j];
	printf("%s", namelist[0]);
	if (desc.indom != PM_INDOM_NULL) {
	    if ((n = pmNameInDom(desc.indom, vp->inst, &p)) < 0)
		printf(" inst [%d]", vp->inst);
	    else {
		printf(" inst [%d or \"%s\"]", vp->inst, p);
		free(p);
	    }
	}
	printf(" old value=");
	pmPrintValue(stdout, vsp->valfmt, desc.type, vp, 1);
	vsp->valfmt = __pmStuffValue(&nav, aggr_len, &vsp->vlist[j], desc.type);
	printf(" new value=");
	pmPrintValue(stdout, vsp->valfmt, desc.type, vp, 1);
	putchar('\n');
    }
    if ((n = pmStore(result)) < 0) {
	printf("%s: pmStore: %s\n", namelist[0], pmErrStr(n));
	exit(1);
    }
    pmFreeResult(result);

    exit(0);
}
