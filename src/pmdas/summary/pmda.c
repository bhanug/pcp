/*
 * Copyright (c) 1995-2002 Silicon Graphics, Inc.  All Rights Reserved.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#include "pmapi.h"
#include "impl.h"
#include "pmda.h"
#include "./summary.h"
#include "./domain.h"

pid_t		clientPID;

int
main(int argc, char **argv)
{
    int			errflag = 0;
    char		*configFile = NULL;
    char		**commandArgv;
    pmdaInterface	dispatch;
    int			i;
    int			len, c;
    extern char		*optarg;
    extern int		optind;
    int			clientPipe[2];
    int			configfd = -1;
    char		*p;
    extern void		summary_init(pmdaInterface *);
    extern void		summary_done(void);
    __pmIPC		ipc = { PDU_VERSION2, NULL };
    char		helpfile[MAXPATHLEN]; 
    int			cmdpipe;		/* metric source/cmd pipe */
    char		*command = NULL;

    /* trim cmd name of leading directory components */
    pmProgname = argv[0];
    for (p = pmProgname; *p; p++) {
	if (*p == '/')
	    pmProgname = p+1;
    }

    __pmSetInternalState(PM_STATE_PMCS);  /* we are below the PMAPI */

    snprintf (helpfile, sizeof(helpfile), "%s/pmdas/summary/help", pmGetConfig("PCP_VAR_DIR"));
    pmdaDaemon (&dispatch, PMDA_INTERFACE_2, pmProgname, SYSSUMMARY,
		"summary.log", helpfile);

    while ((c = pmdaGetOpt(argc, argv, "H:h:D:d:l:c:",
			   &dispatch, &errflag)) != EOF) {
	switch (c) {

	    case 'c':
		configFile = optarg;
		break;

	    case 'H':		/* backwards compatibility, synonym for -h */
		dispatch.version.two.ext->e_helptext = optarg;
		break;

	    case '?':
		errflag++;
		break;
	}
    }

    for (len=0, i=optind; i < argc; i++) {
	len += strlen(argv[i]) + 1;
    }
    if (len == 0) {
	fprintf(stderr, "%s: a command must be given after the options\n",
		pmProgname);
	errflag++;
    }
    else {
	command = (char *)malloc(len+2);
	command[0] = '\0';
	for (i=optind; i < argc; i++) {
	    if (i > optind)
		strcat(command, " ");
	    strcat(command, argv[i]);
	}
    }
    commandArgv = argv + optind;

    if (errflag) {
	fprintf(stderr, "Usage: %s [options] command [arg ...]\n\n",
		pmProgname);
	fputs("Options:\n"
	      "  -h helpfile    help text file\n"
	      "  -c configfile  load ASCII PDUs from config file on startup\n"
	      "  -d domain      use domain (numeric) for metrics domain of PMDA\n"
	      "  -l logfile     write log into logfile rather than using default log name\n",
	      stderr);		
	exit(1);
    }

    /* force errors from here on into the log */
    pmdaOpenLog(& dispatch);

    /* initialize */
    summary_init(&dispatch);

    if (configFile) {
	if ((configfd = open(configFile, O_RDONLY)) < 0) {
	    fprintf(stderr, "%s: failed to open config file ", pmProgname);
	    perror(configFile);
	    exit(1);
	}
	__pmAddIPC(configfd, ipc);
    }

#ifdef MALLOC_AUDIT
    _malloc_reset_();
    atexit(_malloc_audit_);
#endif

    pmdaConnect (&dispatch);
    if ( dispatch.status ) {
	fprintf (stderr, "Cannot connect to pmcd: %s\n",
		 pmErrStr(dispatch.status));
        exit (1); 
    }

    /*
     * open a pipe to the command
     */
    if (pipe(clientPipe) < 0) {
	perror("pipe");
	exit(errno);
    }

    if ((clientPID = fork()) == 0) {
	/* child */
	char cmdpath[MAXPATHLEN+5];
	close(clientPipe[0]);
	if (dup2(clientPipe[1], fileno(stdout)) < 0) {
	    perror("dup");
	    exit(errno);
	}
	close(clientPipe[1]);

        snprintf (cmdpath, sizeof(cmdpath), "exec %s", commandArgv[0]);
	execv(commandArgv[0], commandArgv);
  
	perror(cmdpath);
	exit(errno);
    }

    fprintf(stderr, "clientPID = %d\n", clientPID);

    close(clientPipe[1]);
    cmdpipe = clientPipe[0]; /* parent/agent reads from here */
    __pmAddIPC(cmdpipe, ipc);

    summaryMainLoop(pmProgname, configfd, cmdpipe, &dispatch);

    summary_done();

    exit(0);
}
