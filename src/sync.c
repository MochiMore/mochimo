/**
 * @private
 * @file sync.c
 * @copyright © Adequate Systems LLC, 2018-2021. All Rights Reserved.
 * <br />For more information, please refer to ../LICENSE
*/

/* include guard */
#ifndef MOCHIMO_SYNC_C
#define MOCHIMO_SYNC_C


#include "sync.h"

/* internal support */
#include "types.h"
#include "tfile.h"
#include "network.h"
#include "ledger.h"
#include "global.h"
#include "error.h"
#include "bval.h"
#include "bup.h"

/* external support */
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include "extthrd.h"
#include "extmath.h"
#include "extlib.h"
#include "extint.h"
#include "extio.h"

#define THREADS_MAX  64

typedef struct {
   volatile int tr;  /* thread function result -- set by thread */
   word8 bnum[8];    /* blockchain file to download */
   word32 ip;        /* source ip */
} BIP_THREAD_ARGS;

ThreadProc thread_get_block(void *arg)
{
   BIP_THREAD_ARGS *args = (BIP_THREAD_ARGS *) arg;
   char fname[FILENAME_MAX], fname2[FILENAME_MAX];
   int res;

   /* initialize */
   sprintf(fname, "b%" P32x ".tmp", get32(args->bnum));
   sprintf(fname2, "b%" P32x ".dat", get32(args->bnum));
   res = get_file(args->ip, args->bnum, fname);
   if (res == VEOK) {
      res = rename(fname, fname2);
      if (res != 0) {
         perrno("failed to move %s -> %s", fname, fname2);
         res = VERROR;
      }
   }

   remove(fname);
   args->tr = (res << 8) | 1;
   Unthread;
}

/**
 * Reset chain data from Tfile. Deletes blockchain files above the last
 * Tfile entry, and logs warnings for missing blockchain files. Sets:
 *    Cblocknum, Eon, Time0, Difficulty, Cblock/Prevhash, and Weight.
 * @returns (int) value representing operation success
 * @retval VERROR on error; check errno for details
 * @retval VEOK on success
 */
int reset_chain(void)
{
   BTRAILER bt;
   word8 bnum[8];
   char fname[FILENAME_MAX];
   char bcfname[FILENAME_MAX];

   /* obtain latest block trailer from Tfile */
   if (read_trailer(&bt, "tfile.dat") != VEOK) return VERROR;
   /* check blockchain files -- delete overrun */
   for (put64(bnum, bt.bnum); ; add64(bnum, ONE64, bnum)) {
      bnum2fname(bnum, bcfname);
      path_join(fname, Bcdir, bcfname);
      if (cmp64(bnum, bt.bnum) == 0) {
         /* check we have the latest block from Tfile */
         if (!fexists(fname)) {
            perrno("missing blockchain file %s", fname);
            return VERROR;
         }
      } else {
         /* delete blockchain files above Tfile */
         if (!fexists(fname)) break;
         if (remove(fname) != 0) {
            perrno("failed to remove %s", fname);
            return VERROR;
         }
      }
   }  /* end for() */

   /* initialize chain data from block trailer */
   put64(Cblocknum, bt.bnum);
   Eon = get32(bt.bnum) >> 8;
   Time0 = get32(bt.stime);
   Difficulty = next_difficulty(&bt);
   memcpy(Prevhash, bt.phash, HASHLEN);
   memcpy(Cblockhash, bt.bhash, HASHLEN);

   /* Re-compute Weight[] -- check double bnum */
   if (weigh_tfile("tfile.dat", bnum, Weight) != VEOK) {
      perrno("weight_tfile() FAILURE");
      return VERROR;
   } else if (cmp64(bnum, Cblocknum) != 0) {
      perr("block number mismatch!");
      return VERROR;
   }

   return VEOK;
}  /* end reset_chain() */

/* Extract Genesis Block to ledger.dat */
int extract_gen(char *lfile)
{
   char fname[FILENAME_MAX];

   path_join(fname, Bcdir, "b0000000000000000.bc");
   /* extract the ledger from our Genesis Block */
   return le_extract(fname, lfile);
}

/**
 * Catch up by getting blocks from peers in plist[count].
 * Returns VEOK if updates made, else b_update() error code. */
int catchup(word32 plist[], word32 count)
{
   char fname[FILENAME_MAX], fname2[FILENAME_MAX];
   ThreadId tid[MAXQUORUM] = { 0 };
   BIP_THREAD_ARGS args[MAXQUORUM] = { 0 };
   word8 bnum[8], bclear[8];
   word32 i, n, done;
   FILE *fp;
   int res;

   /* initialize... */
   show("getblock");  /* get blockchain files */
   pdebug("catchup(%" P32u " peers): begin...", count);
   if (mkdir_p(Bcdir) != 0) {  /* ensure Bcdir is ready */
      perrno("failed to verify %s/ directory", Bcdir);
      return VERROR;
   }  /* fill args with peer ips */
   for (done = n = 0; n < MAXQUORUM && n < count; n++) {
      args[n].ip = plist[n];
   }

   /* download/validate/update blocks from args */
   put64(bclear, Cblocknum);
   while(Running && done < n) {
      for(put64(bnum, Cblocknum), done = i = 0; i < n; i++) {
         if (args[i].ip == 0) done++;
         else if (tid[i] > 0 && args[i].tr) {  /* thread finished */
            if (thread_join(tid[i]) != 0) perrno("thread_join");
            if ((args[i].tr >> 8) != VEOK) args[i].ip = 0;  /* kick */
            args[i].tr = 0;
            tid[i] = 0;
         } else if (tid[i] == 0) {
            do {  /* determine next required block - skip neogenesis */
               add64(bnum, One, bnum);
               if (bnum[0] == 0) add64(bnum, One, bnum);
               sprintf(fname, "b%" P32x ".tmp", get32(bnum));
               sprintf(fname2, "b%" P32x ".dat", get32(bnum));
               if (cmp64(bnum, bclear) > 0) {
                  /* clear a safe path for the incoming blocks */
                  put64(bclear, bnum);
                  remove(fname2);
                  remove(fname);
               }  /* ... path is clear */
            } while(fexists(fname) || fexists(fname2));
            /* create file for child, so the children don't fight */
            fp = fopen(fname, "w");
            if (fp == NULL) perrno("fopen(%s) failed", fname);
            else {
               fclose(fp);
               put64(args[i].bnum, bnum);
               res = thread_create(&(tid[i]), &thread_get_block, &args[i]);
               if (res != 0) {
                  perrno("thread_create");
                  args[i].tr = 0;
                  tid[i] = 0;
                  remove(fname);
               }
            }
         }
      }
      do {
         add64(Cblocknum, One, bnum);
         sprintf(fname2, "b%" P32x ".dat", get32(bnum));
         if (fexists(fname2)) {
            res = b_update(fname2, 0);
            if (res != VEOK) {
               perr("failed to update block file %s", fname2);
               /* wait for all threads to finish and return res */
               for (i = 0; i < n; i++) {
                  if (tid[i] == 0) continue;
                  if (thread_cancel(tid[i]) != 0) {
                     perrno("thread_cancel(%zu)", (size_t) tid[i]);
                  }
               }
               return res;
            }
         }
      } while(Running && cmp64(Cblocknum, bnum) == 0);
      if(Dynasleep) usleep(Dynasleep);  /* small rest */
   }  /* end while(Running && done < n... download blocks */

   return VEOK;
}  /* end catchup() */

/**
 * Resynchronize blockchain up to network weight/bnum using quorum[qidx].
 * Returns VEOK on success, else restarts. */
int resync(word32 quorum[], word32 *qidx, void *highweight, void *highbnum)
{
   static word8 num256[8] = { 0, 1, };
   char ipaddr[16], fname[FILENAME_MAX], bcfname[21];
   word8 bnum[8], weight[HASHLEN];

   show("gettfile");  /* get tfile */
   pdebug("fetching tfile.dat from %s", ntoa(&quorum[0], ipaddr));
   while(Running && *quorum) {
      remove("tfile.dat");
      if (get_file(*quorum, NULL, "tfile.tmp") == VEOK) {
         if (rename("tfile.tmp", "tfile.dat") == 0) break;
         perrno("failed to rename tfile.dat");
      }
      /* remove quorum member, and try again */
      remove32(*quorum, quorum, *qidx, qidx);
   }
   if (!(*quorum)) restart("gettfile no quorum");
   if (!Running) resign("gettfile exiting");

   show("tfval");  /* validate tfile */
   if (validate_tfile("tfile.dat", bnum, weight, 0)) return VERROR;
   else pdebug("tfile.dat is valid.");
   if (cmp256(weight, highweight) >= 0 && cmp64(bnum, highbnum) >= 0) {
      pdebug("tfile.dat matches advertised bnum and weight.");
   } else return VERROR;
   if (!(*quorum)) restart("tfval no quorum");
   if (!Running) resign("tfval exiting");

   /* determine starting neo-genesis block */
   put64(bnum, highbnum); bnum[0] = 0;
   if (sub64(bnum, num256, bnum)) memset(bnum, 0, 8);
   pdebug("neo-genesis block 0x%s", bnum2hex(bnum, NULL));
   /* trim the tfile back to the neo-genesis block and close the ledger */
   if (trim_tfile("tfile.dat", bnum) != VEOK) restart("getneo tfile_trim()");  /* panic */
   le_close();  /* close ledger, we're gonna grab a new one... */
   /* download neo-genesis block if no backup */
   if(!iszero(bnum, 8)) {  /* ... no need to download genesis block */
      plog("downloading neo-genesis block 0x%s", bnum2hex(bnum, NULL));
      while(Running && *quorum) {
         show("getneo");
         remove("ngblock.dat");
         if (get_file(*quorum, bnum, "ngblock.dat") == VEOK) {
            show("checkneo");
            /* validate neogenesis block */
            if (ng_val("ngblock.dat", bnum) != VEOK) {
               perrno("Bad NG block");
               remove("ngblock.dat");
            } else break;
         }
         /* remove quorum member, and try again */
         remove32(*quorum, quorum, *qidx, qidx);
      }
      if (!(*quorum)) restart("getneo no quorum");
      if (!Running) resign("getneo exiting");
      /* transfer neo-genesis block to bcdir */
      bnum2fname(bnum, bcfname);
      path_join(fname, Bcdir, bcfname);
      if(rename("ngblock.dat", fname) != 0) {
         perrno("cannot move neo-genesis to %s", fname);
         return VERROR;
      }
      /* extract ledger from neo-genesis block... */
      if(le_extract(fname, "ledger.dat") != VEOK) {
         restart("getneo ledger extraction");
      }  /* ... or from genesis block */
   } else extract_gen("ledger.dat");

   show("setdiff");  /* setup difficulty, based on [neo]genesis block */
   if(reset_chain() != VEOK) restart("setdiff reset");
   le_open("ledger.dat");

   /* get blockchain */
   if (catchup(quorum, *qidx) != VEOK) {
      plog("catchup() encountered an error, restarting...");
      restart("catchup error");
   }

   /* Post-sync hook for external SQL database export */
   /* Shell script in /bin directory */
   if(Exportflag && fexists("../init-external.sh")) {
     plog("Calling ../init-external.sh\n");  /* first time call */
     system("../init-external.sh");
   }

   if(!Running) resign("quorum update");
   plog("\nVeronica says, 'You're done!'");

   /* Done! */
   return VEOK;
}

/* Pull a divergent block chain and merge it into ours
 * rather than bailing out to contention!
 * Always returns VEOK to ignore contention.
 * splitblock is where the two chains diverge.
 * txcblock is the advertised block of peer,
 * and peerip is its ip address.
 */
int syncup(word32 splitblock, word8 *txcblock, word32 peerip)
{
   word8 bnum[8], tfweight[HASHLEN];
   word8 lastneo[8], sblock[8];
   char buff[256], bcfname[21];
   int j;
   NODE *np2;
   time_t lasttime;

   Insyncup = 1;
   show("syncup");

   /* Stop constructing and sending update blocks, since we're behind */
   stop_bcon();
   stop_found();

   /* Stop block transfer children and others */
   for(np2 = Nodes; np2 < Hi_node; np2++) {
      if(np2->pid == 0) continue;
      kill(np2->pid, SIGTERM);
      waitpid(np2->pid, NULL, 0);
      freeslot(np2);
   }

   /* Close server ledger */	
   pdebug("beginning state save...");
   le_close();

   /* Backup TFILE, Ledger, and blocks to split-tree directory. */
   /* system("mkdir split"); * already exists */
   pdebug("Backing up TFILE, ledger.dat, and blocks...");
   system("rm -f split/*");  /* don't complain, just do it */
   system("cp tfile.dat split");
   system("cp ledger.dat split");
   system("mv bc/*.bc split");

   put32(sblock + 4, 0);
   put32(sblock, splitblock);
   /* Compute first previous NG block */
   if (sub64(lastneo, CL64_32(0x100), lastneo)) {
      memset(lastneo, 0, 8);
   } else lastneo[0] = 0;
   bnum2fname(lastneo, bcfname);
   pdebug("Identified first previous NG block as %s", bcfname);

   /* Delete Ledger and trim T-File */
   if(remove("ledger.dat") != 0) {
      pdebug("syncup() failed!  Unable to delete ledger.dat");
      goto badsyncup;
   }
   if(trim_tfile("tfile.dat", lastneo) != VEOK) {
      pdebug("T-File trim failed!");
      goto badsyncup;
   }

   /* Extract first previous Neogenesis Block to ledger.dat */
   pdebug("Expanding Neo-genesis block to ledger.dat...");
   sprintf(buff, "cp split/%s bc/%s", bcfname, bcfname);
   system(buff);
   sprintf(buff, "bc/%s", bcfname);
   if(le_extract(buff, "ledger.dat") != VEOK) {
      pdebug("failed!  Unable to extract ledger!");
      goto badsyncup;
   }

   /* setup Difficulty and globals, based on neogenesis block */
   if(reset_chain() != VEOK) {
      pdebug("failed!  reset_chain() failed!");
      goto badsyncup;
   }
   le_open("ledger.dat");

   pdebug("Split point is block %s", bnum2hex(sblock, NULL));
   add64(lastneo, One, bnum);
   for( ;cmp64(bnum, sblock) < 0; ) {
      bnum2fname(bnum, bcfname);
      pdebug("Copying split/%s to spblock.tmp", bcfname);
      sprintf(buff, "cp split/%s spblock.tmp", bcfname);
      system(buff);
      if(b_update("spblock.tmp", 1) != VEOK) {
         pdebug("failed to update our own block.");
         goto badsyncup;
      }
      add64(bnum, One, bnum);
      if(bnum[0] == 0) add64(bnum, One, bnum);  /* skip NG blocks */
   }

   /* Download missing blocks from peer. */
   pdebug("Download and update missing blocks from peer...");
   put64(bnum, sblock);
   for(j = 0; ; ) {
      if(bnum[0] == 0) add64(bnum, One, bnum);  /* skip NG blocks */
      sprintf(buff, "b%s.dat", bnum2hex64(bnum, bcfname));
      if(j == 60) {
         pdebug("failed while downloading %s from %s",
                        buff, ntoa(&peerip, NULL));
         goto badsyncup;
      }
      lasttime = time(NULL);
      if(get_file(peerip, bnum, buff) != VEOK) {
         if(cmp64(bnum, txcblock) >= 0) break;  /* success */
         if(time(NULL) == lasttime) sleep(1);
         j++;  /* retry counter */
         continue;
      }
      if(b_update(buff, 0) != VEOK) {
         pdebug("cannot update peer's block.");
         goto badsyncup;
      }
      add64(bnum, One, bnum);
   }
   system("cp split/b0000000000000000.bc bc");
   system("rm split/*");
   /* re-compute tfile weight */
   if(weigh_tfile("tfile.dat", bnum, tfweight)) {
      plog("tf_val() error");
   } else plog("syncup() is good!");
   memcpy(Weight, tfweight, HASHLEN);
   Insyncup = 0;
   return VEOK;

badsyncup:
   /* Restore block chain from saved state after a bad re-sync attempt. */
   pdebug("bad sync: restoring saved state...");
   le_close();
   system("mv split/tfile.dat .");
   system("mv split/ledger.dat .");
   system("rm *.bc bc/*");
   system("mv split/* bc");
   reset_chain();  /* reset Difficulty and others */
   le_open("ledger.dat");
   Insyncup = 0;
   return VEOK;
}  /* end syncup() */

/* Handle contention
 * Returns:  0 = nothing else to do
 *           1 = do fetch block with child
 */
int contention(NODE *np)
{
   word32 splitblock;
   TX *tx;
   word32 j;
   int result;
   BTRAILER *bt, *prev_bt, our_bt[NTFTX];
   word8 weight[32];

   pdebug("IP: %s", ntoa(&np->ip, NULL));

   tx = &np->tx;
   /* ignore low weight */
   if(cmp256(tx->weight, Weight) <= 0) {
      pdebug("Ignoring low weight");
      return 0;
   }
   /* ignore NG blocks */
   if(tx->cblock[0] == 0) {
      epinklist(np->ip);
      return 0;
   }

   if(memcmp(Cblockhash, tx->pblockhash, HASHLEN) == 0) {
      pdebug("get the expected block");
      return 1;  /* get block */
   }

   /* Try to do a simple catchup() of more than 1 block on our own chain. */
   j = get32(tx->cblock) - get32(Cblocknum);
   if(j > 1 && j <= NTFTX) {
        bt = (BTRAILER *) tx->buffer;  /* top of tx proof array */
        /* Check for matching previous hash in the array. */
        if(memcmp(Cblockhash, bt[NTFTX - j].phash, HASHLEN) == 0) {
           result = catchup(&np->ip, 1);
           if(result == VEOK) goto done;  /* we updated */
           if(result == VEBAD) return 0;  /* EVIL: ignore bad bval2() */
        }
   }
   /* Catchup failed so check the tx proof and chain weight. */

   /* check existance of, and that we've received enough proof */
   if (cmp64(tx->cblock, CL64_32(NTFTX)) < 0) return 0;
   if ((get16(tx->len) / sizeof(BTRAILER)) < NTFTX) return 0;

   bt = (BTRAILER *) tx->buffer;

   /* read our Tfile data and compare their low trailer -- MUST MATCH */
   if (read_tfile(our_bt, bt->bnum, NTFTX, "tfile.dat") <= 0) return 0;
   if (memcmp(bt, our_bt, sizeof(BTRAILER)) != 0) return 0;

   /* compute previous weight for add_weight() */
   memcpy(weight, Weight, 32);
   if (past_weight("tfile.dat", bt->bnum, weight) != VEOK) return 0;

   /* scan trailer array... */
   for (j = 1; j < NTFTX; j++) {
      bt = &((BTRAILER *) tx->buffer)[j];
      prev_bt = &((BTRAILER *) tx->buffer)[j - 1];
      /* ... validate their trailer proof (incl. PoW), and add weight */
      if (validate_trailer(bt, prev_bt) != VEOK) return 0;
      if (get32(bt->tcount) && validate_pow(bt) != VEOK) return 0;
      add_weight(weight, bt->difficulty[0]);
      /* ... check for splitblock */
      if (splitblock == 0) {
         if (memcmp(bt, &(our_bt[j]), sizeof(BTRAILER)) == 0) {
            splitblock = get32(bt->bnum);
         }
      }
   }

   /* check weight weight is as advertised and non-zero splitblock */
   if (memcmp(weight, tx->weight, 32) != 0) return 0;
   if (splitblock == 0) return 0;

   /* Proof is good so try to re-sync to peer */
   if(syncup(splitblock, tx->cblock, np->ip) != VEOK) return 0;
done:
   /* send_found on good catchup or syncup */
   send_found();  /* start send_found() child */
   addrecent(np->ip);
   return 0;  /* nothing else to do */
}  /* end contention() */

/* end include guard */
#endif
