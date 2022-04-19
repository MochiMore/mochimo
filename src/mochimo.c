/* mochimo.c
 *
 * Copyright (c) 2019 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.PDF   **** NO WARRANTY ****
 *
 * The Mochimo Project System Software
 * This file builds a server.
 *
 * Revised: 20 August 2018
*/

/* include guard */
#ifndef MOCHIMO_C
#define MOCHIMO_C


/* build sequence */
#define PATCHLEVEL 37
#define VERSIONSTR  "2.4.2-rc1"   /*   as printable string */

/* system support */
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>  /* for waitpid() */
#include <sys/file.h>  /* for flock() */
#include <fcntl.h>

/* standard-c support */
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <signal.h>

/* extended-c support */
#include "extinet.h"    /* socket support */
#include "extlib.h"     /* general support */
#include "extmath.h"    /* 64-bit math support */
#include "extprint.h"   /* print/logging support */

/* Include everything that we need */
#include "config.h"
#include "proto.h"

/* Include global data . . . */
#include "data.c"       /* System wide globals  */

/* crypto supprt functions  */
#include "crc16.h"
#include "crc32.h"      /* for mirroring          */
#include "sha256.h"

/* Server control */
#include "util.c"       /* server support */
#include "pink.c"       /* manage pinklist                 */
#include "ledger.c"
#include "gettx.c"      /* poll and read NODE socket       */
#include "txval.c"      /* validate transactions           */
#include "mirror.c"
#include "execute.c"
#include "monitor.c"    /* system monitor/debugger prompt  */
#include "daemon.c"
#include "bupdata.c"    /* for block updates               */
#include "miner.c"
#include "pval.c"       /* pseudo-blocks                   */
#include "optf.c"       /* for OP_HASH and OP_TF           */
#include "proof.c"
#include "renew.c"
#include "update.c"
#include "init.c"       /* read Coreplist[] and get_eon()  */
#include "syncup.c"     /* Resync Node on Inferior Chain   */
#include "server.c"     /* tcp server                      */


void usage(void)
{
   printf("usage: mochimo [-option...]\n"
          "         -l         open mochi.log file\n"
          "         -lFNAME    open log file FNAME\n"
          "         -e         enable error.log file\n"
          "         -tN        set Trace to N (0, 1, 2, 3)\n"
          "         -qN        set Quorum to N (default 4)\n"
          "         -vN        set virtual mode: N = 1 or 2\n"
          "         -cFNAME    read core ip list from FNAME\n"
          "         -LFNAME    read local peer ip list from FNAME\n"
          "         -c         disable read core ip list\n"
          "         -d         disable pink lists\n"
          "         -pN        set port to N\n"
          "         -D         Daemon ignore ctrl-c and no term output\n"
          "         -sN        sleep N usec. on each loop if not busy\n"
          "         -xxxxxxx   replace xxxxxxx with state\n"
          "         -f         frisky mode (promiscuous mirroring)\n"
          "         -S         Safe mode\n"
          "         -F         Filter private IP's\n"  /* v.28 */
          "         -P         Allow pushed mblocks\n"
          "         -n         Do not solve blocks\n"
          "         -Mn        set transaction fee to n\n"
          "         -Sanctuary=N,Lastday\n"
          "         -Tn        set Trustblock to n for tfval() speedup\n"
   );
#ifdef BX_MYSQL
   printf("         -X         Export to MySQL database on block update\n");
#endif
   exit(0);
}


void veronica(void)
{
   byte h[64];
   char *cp;

   cp = trigg_generate(h, 0);
   if(cp) printf("\n%s\n\n", cp);
   exit(0);
}

/* Kill the miner child */
int stop_miner(void)
{
   int status;

   if(Mpid == 0) return -1;
   kill(Mpid, SIGTERM);
   waitpid(Mpid, &status, 0);
   Mpid = 0;
   return status;
}


/* Display terminal error message
 * and exit with exitcode after reaping zombies.
 */
void fatal2(int exitcode, char *message)
{
   stop_miner();
   if(Sendfound_pid) kill(Sendfound_pid, SIGTERM);
#ifndef EXCLUDE_NODES
   stop_mirror();
#endif
   if(!Bgflag && message) {
      error("%s", message);
      fprintf(stdout, "fatal: %s\n", message);
   }
   /* wait for all children */
   while(waitpid(-1, NULL, 0) != -1);
   exit(exitcode);
}

/* Display terminal error message
 * and exit with NO restart (code 0).
 */
#define fatal(mess) fatal2(0, mess)
#define pause_server() fatal2(0, NULL);

void restart(char *mess)
{
   unlink("epink.lst");
   stop_miner();
   if(Trace && mess != NULL) plog("restart: %s", mess);
   fatal2(1, NULL);
}

char *show(char *state)
{
   if(state == NULL) state = "(null)";
   if(Statusarg) strncpy(Statusarg, state, 8);
   return state;
}


/* 
 * Initialise data and call the server.
 */

int main(int argc, char **argv)
{
   static int j;
   static byte endian[] = { 0x34, 0x12 };
   static char *cp;

   /* sanity checks */
   if(sizeof(word32) != 4) fatal("word32 should be 4 bytes");
   if(sizeof(TX) != TXBUFFLEN || sizeof(LTRAN) != (TXADDRLEN + 1 + TXAMOUNT)
      || sizeof(BTRAILER) != BTSIZE)
      fatal("struct size error.\nSet compiler options for byte alignment.");    
   if(get16(endian) != 0x1234)
      fatal("little-endian machine required for this build.");

   /* improve random generators w/additional entropy from maddr.dat */
   read_data(Maddr, TXADDRLEN, "maddr.dat");
   srand16fast(time(&Ltime) ^ get32(Maddr) ^ getpid());
   srand16(Ltime ^ get32(Maddr+4), 0, 123456789 ^ get32(Maddr+8) ^ getpid());

   Port = Dstport = PORT1;    /* default receive port */
   /*
    * Parse command line arguments.
    */
   for(j = 1; j < argc; j++) {
      if(argv[j][0] != '-') usage();
      switch(argv[j][1]) {
         case 't':  Trace = atoi(&argv[j][2]); /* set trace level  */
                    break;
         case 'q':  Quorum = atoi(&argv[j][2]); /* peers in gang[Quorum] */
                    if((unsigned) Quorum > MAXQUORUM) usage();
                    break;
         case 'p':  Port = Dstport = atoi(&argv[j][2]); /* home/dst */
                    break;
         case 'l':  if(argv[j][2]) /* open log file used by plog()   */
                       set_output_file(&argv[j][2], "a");
                    else set_output_file(LOGFNAME, "a");
                    Cbits |= C_LOGGING;
                    break;
         case 'e':  Errorlog = 1;  /* enable "error.log" file */
                    break;
         case 'c':  Corefname = &argv[j][2];  /* master network */
                    break;
         case 'L':  Lpfname = &argv[j][2];  /* local peer network */
                    break;
         case 'd':  Disable_pink = 1;  /* disable pink lists */
                    break;
         case 'f':  Frisky = 1;
                    break;
         case 'S':  if(strncmp(argv[j], "-Sanctuary=", 11) == 0) {
                       cp = strchr(argv[j], ',');
                       if(cp == NULL) usage();
                       Sanctuary = strtoul(&argv[j][11], NULL, 0);
                       Lastday = (strtoul(cp + 1, NULL, 0) + 255) & 0xffffff00;
                       Cbits |= C_SANCTUARY;
                       printf("\nSanctuary=%u  Lastday 0x%0x...",
                              Sanctuary, Lastday);  fflush(stdout); sleep(2);
                       printf("\b\b\b accounted for.\n");  sleep(1);
                       break;
                    }
                    if(argv[j][2]) usage();
                    Safemode = 1;
                    break;
         case 'n':  Nominer = 1;
                    break;
         case 'F':  Noprivate = 1;  /* v.28 */
                    break;
         case 'P':  Allowpush = 1;  Cbits |= C_PUSH;
                    break;
         case 'x':  if(strlen(argv[j]) != 8) break;
                    Statusarg = argv[j];
                    break;
         case 'D':  Bgflag = 1;
                    break;
         case 's':  Dynasleep = atoi(&argv[j][2]);  /* usleep time */
                    break;
         case 'M':  Myfee[0] = atoi(&argv[j][2]);
                    if(Myfee[0] < Mfee[0]) Myfee[0] = Mfee[0];
                    else Cbits |= C_MFEE;
                    break;
         case 'V':  if(strcmp(&argv[j][1], "Veronica") == 0)
                       veronica();
                    usage();
         case 'v':  Dstport = PORT2;  Port = PORT1;
                    if(argv[j][2] == '2') {
                       Dstport = PORT1;  Port = PORT2;
                    }
                    break;
         case 'T':  if(argv[j][2] == '\0') {
                       Trustblock = -1;
                       break;
                    }
                    else {
                       Trustblock = atoi(&argv[j][2]);
                       break;
                    }
#ifdef BX_MYSQL
         case 'X':  Exportflag = 1;
                    break;
#endif
         default:   usage();
      }  /* end switch */
   }  /* end for j */

   if(Trace == 3) { Trace = 0; Betabait = 1; }
   if(Bgflag) setpgrp();  /* detach */

   /*
    * Redirect signals.
    */
   fix_signals();
   signal(SIGCHLD, SIG_DFL);  /* so waitpid() works */

   init();  /* fetch initial block chain */
   if(!Bgflag) printf("\n");

   plog("\nMochimo Server (Build %d)  PVERSION: %d  Built on %s %s\n"
        "Copyright (c) 2019 Adequate Systems, LLC.  All rights reserved.\n"
        "\nBooting",
        PATCHLEVEL, PVERSION, __DATE__, __TIME__);

   /* 
    * Show local host info
    */
   if(!Bgflag) phostinfo();

   server();                  /* start server */

   plog("Server exiting . . .");
   save_rplist();
   savepink();
   pause_server();
   return 0;              /* never gets here */
} /* end main() */

/* end include guard */
#endif