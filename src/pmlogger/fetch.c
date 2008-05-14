/*
 * Copyright (c) 1995 Silicon Graphics, Inc.  All Rights Reserved.
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
#include <sys/time.h>
#include "pmapi.h"
#include "impl.h"
#include "./logger.h"

int
myFetch(int numpmid, pmID pmidlist[], __pmPDU **pdup)
{
    int			n = 0;
    int			ctx;
    __pmPDU		*pb;
    __pmContext		*ctxp;

    if (numpmid < 1)
	return PM_ERR_TOOSMALL;

    if ((ctx = pmWhichContext()) >= 0) {
	ctxp = __pmHandleToPtr(ctx);
	if (ctxp->c_type != PM_CONTEXT_HOST)
	    return PM_ERR_NOTHOST;
    }
    else
	return PM_ERR_NOCONTEXT;

#if CAN_RECONNECT
    if (ctxp->c_pmcd->pc_fd == -1) {
	/* lost connection, try to get it back */
	n = reconnect();
	if (n < 0)
	    return n;
    }
#endif

    if (ctxp->c_sent == 0) {
	/*
	 * current profile is _not_ already cached at other end of
	 * IPC, so send current profile
	 */
#ifdef PCP_DEBUG
	if (pmDebug & DBG_TRACE_PROFILE)
	    fprintf(stderr, "myFetch: calling __pmSendProfile, context: %d\n", ctx);
#endif
	if ((n = __pmSendProfile(ctxp->c_pmcd->pc_fd, PDU_BINARY, ctx, ctxp->c_instprof)) >= 0)
	    ctxp->c_sent = 1;
    }

    if (n >= 0) {
	n = __pmSendFetch(ctxp->c_pmcd->pc_fd, PDU_BINARY, ctx, &ctxp->c_origin, numpmid, pmidlist);
	if (n >= 0){
	    int		changed = 0;
	    do {
		n = __pmGetPDU(ctxp->c_pmcd->pc_fd, PDU_BINARY, TIMEOUT_DEFAULT, &pb);
		/*
		 * expect PDU_RESULT or
		 *        PDU_ERROR(changed > 0)+PDU_RESULT or
		 *        PDU_ERROR(real error < 0 from PMCD) or
		 *        0 (end of file)
		 *        < 0 (local error or IPC problem)
		 *        other (bogus PDU)
		 */
		if (n == PDU_RESULT)
		    /* success */
		    *pdup = pb;
		else if (n == PDU_ERROR) {
		    __pmDecodeError(pb, PDU_BINARY, &n);
		    if (n > 0) {
			/* PMCD state change protocol */
			changed = n;
			n = 0;
		    }
		    else {
			fprintf(stderr, "myFetch: ERROR PDU: %s\n", pmErrStr(n));
			disconnect(PM_ERR_IPC);
		    }
		}
		else if (n == 0) {
		    fprintf(stderr, "myFetch: End of File: PMCD exited?\n");
		    disconnect(PM_ERR_IPC);
		}
		else if (n < 0) {
		    fprintf(stderr, "myFetch: __pmGetPDU: Error: %s\n", pmErrStr(n));
		    disconnect(PM_ERR_IPC);
		}
		else {
		    fprintf(stderr, "myFetch: Unexpected %s PDU from PMCD\n", __pmPDUTypeStr(n));
		    disconnect(PM_ERR_IPC);
		}
	    } while (n == 0);

	    if (changed & PMCD_ADD_AGENT) {
		/*
		 * PMCD_DROP_AGENT does not matter, no values are returned.
		 * Trying to restart (PMCD_RESTART_AGENT) is less interesting
		 * than when we actually start (PMCD_ADD_AGENT) ... the latter
		 * is also set when a successful restart occurs, but more
		 * to the point the sequence Install-Remove-Install does
		 * not involve a restart ... it is the second Install that
		 * generates the second PMCD_ADD_AGENT that we need to be
		 * particularly sensitive to, as this may reset counter
		 * metrics ...
		 */
		int	sts;
		if ((sts = putmark()) < 0) {
		    fprintf(stderr, "putmark: %s\n", pmErrStr(sts));
		    exit(1);
		}
	    }
	}
    }

    if (n < 0 && ctxp->c_pmcd->pc_fd != -1) {
	disconnect(n);
    }

    return n;
}
