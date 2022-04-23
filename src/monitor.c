/* monitor.c  Mochimo System Monitor
 *
 * Copyright (c) 2019 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.PDF   **** NO WARRANTY ****
 *
 * Date: 3 January 2018
*/


word32 Hps; /* haiku per second from miner.c hps.dat */

char *tgets(char *buff, int len)
{
   char *cp;

   if(fgets(buff, len, stdin) == NULL)
      *buff = '\0';
   cp = strchr(buff, '\n');
   if(cp) *cp = '\0';
   return buff;
}


/* Display system statistics */
int stats(int showflag)
{

   if(showflag == 0) {
      if(Bgflag == 1 || Trace) return 0;
   }

   printf(     "Status:\n\n"
               "   Aeon:          %u\n"
               "   Generation:    %u\n"
               "   Online:        %u\n"
               "   Raw TX in:     %u\n"
               "   Bad peers:     %u\n"
               "   No space:      %u\n"
               "   Client timeouts: %u\n"
               "   Server errors:   %u\n"
               "   TX recvd:        %u\n"
               "   Balances sent:   %u\n"
               "   TX dups:         %u\n"
               "   txq1 count:      %u\n"
               "   Sends blocked:   %u\n"
               "   Blocks solved:   %u\n"
               "   Blocks updated:  %u\n"
               "\n",
                Eon, Ngen,
                Nonline, Nlogins, Nbadlogs, Nspace, Ntimeouts,
                get_num_errs(), Nrec, Nsent, Ndups, Txcount, Nsenderr,
                Nsolved, Nupdated
   );

   printf("Current block: 0x%s\n", bnum2hex(Cblocknum));
   printf("Weight:        0x...%s\n"
          "Difficulty:    %d  %s\n", bnum2hex(Weight),
          Difficulty, Mpid ? "solving..." : "waiting for tx...");
   return 0;
} /* end stats() */


/* short stat display */
void betabait(void)
{
   word32 hps; /* haiku per second from miner.c hps.dat */

   if(read_data(&hps, sizeof(hps), "hps.dat") == sizeof(hps))
      Hps = hps;

   printf(     "Status:\n\n"
               "   Aeon:          %u\n"
               "   Generation:    %u\n"
               "   Online:        %u\n"
               "   TX recvd:        %u\n"
               "   Balances sent:   %u\n"
               "   Blocks solved:   %u\n"
               "   Blocks updated:  %u\n"
               "   Haiku/second:    %u %s\n"
               "\n",

                Eon, Ngen,
                Nonline,  Nrec, Nsent, Nsolved, Nupdated,
                (word32) Hps, Hps ? "" : "(calculated after 2 TXs/updates)"
   );
   printf("Current block: 0x%s\n"
          "Difficulty:    %d  %s\n\n", bnum2hex(Cblocknum),
          Difficulty, Mpid ? "solving..." : "waiting for TX...");
} /* end betabait() */


void displaycp(void)
{
   int j, k;

   printf("\nTrusted peer list:\n");
   for(j = k = 0; j < TPLISTLEN && Tplist[j]; j++) {
      if(++k > 4) { printf("\n");  k = 0; }
      printf("   %-15.15s", ntoa(&Tplist[j], NULL));
   }
   printf("\n\nRecent peer list:\n");
   for(j = k = 0; j < RPLISTLEN && Rplist[j]; j++) {
      if(++k > 4) { printf("\n");  k = 0; }
      printf("   %-15.15s", ntoa(&Rplist[j], NULL));
   }
   printf("\n\n");
}


void monitor(void)
{
   static word8 runmode = 0;   /* 1 for single stepping */
   static char buff[81], cmd;
   static char logfile[81] = LOGFNAME;     /* log file name */

   /*
    * Print banner if not single stepping.
    */
   if(runmode == 0)
      printf("\n\n\nMochimo System Monitor ver %s\n\n"
             "? for help\n\n", VER_STR);

   show("monitor");
   /*
    * Command loop.
    */
   for(;;) {
      printf("mochimo> ");
      tgets(buff, 80);
      cmd = buff[0];

      if(cmd == '\0') {       /* ENTER to continue server */
         Monitor = runmode;
         printf("In server() loop...\n");
         return;
      }
      if(cmd == 'q') {        /* signal server to exit */
         Running = 0;
         Monitor = runmode;
         return;
      }
      if(cmd == 'r') {        /* restart command */
         printf("Confirm restart (y/n)? ");
         tgets(buff, 80);
         if(*buff != 'y' && *buff != 'Y')
            continue;
         restart("monitor");
      }
      if(cmd == 't') {        /* trace command */
         printf("Trace (0-3): ");
         tgets(buff, 80);
         if(*buff) {
            Trace = atoi(buff);
            if(Trace == 0) Betabait = 0;
            if(Trace == 3) { Trace = 0; Betabait = 1; }
            write_global();
         }
         continue;
      }
      if(cmd == 'm') {        /* mining mode */
setmining:
         printf("Do you want to mine (yes/no) [%s]? ",
                Nominer ? "no" : "yes");
         tgets(buff, 80);
         if(*buff) {
            if(*buff == 'y') { Nominer = 0; continue; }
            if(*buff != 'n') goto setmining;
            Nominer = 1;
         }
         continue;
      }
      if(cmd == 'l') {        /* toggle log file */
         set_output_file(NULL, NULL);
         printf("Log file is closed.\n");
         printf("Log file [%s]: ", logfile);
         tgets(buff, 80);
         if(*buff) strncpy(logfile, buff, 80);
         buff[80] = '\0';
         if(set_output_file(logfile, "a"))
            printf("Cannot open %s\n", logfile);
         else
            printf("Log file %s is open.\n", logfile);
         continue;
      }
      if(cmd == 'e') {           /* toggle error log file */
         Errorlog = 1 - Errorlog;
         if(Errorlog)
            printf("error.log is active.\n");
         else
            printf("error.log is paused.\n");
         write_global();
         continue;
      }
      if(strcmp(buff, "p") == 0) {
         displaycp();  /* current peer list */
         continue;
      }
      if(strncmp(buff, "si", 2) == 0) {   /* single step command */
         runmode = 1 - runmode;
         printf("Single step is ");
         if(runmode) printf("on.\n"); else printf("off.\n");
         continue;
      }
      if(strncmp(buff, "st", 2) == 0) {   /* status command */
         stats(1);
         continue;
      }

      /*
       * Print help message.
       */
      printf("\nCommands:\n\n"
             "<enter> resume server\n"
             "q       quit server\n"
             "r       restart server\n"
             "t       set debug trace level\n"
             "l       toggle log file\n"
             "e       toggle error log\n"
             "p       display peer lists\n"
             "m       set mining mode\n"
             "si      toggle single step mode\n"
             "st      display system status\n"
             "?       this message\n\n" );

   } /* end command loop */
} /* end monitor() */
