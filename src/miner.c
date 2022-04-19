/* miner.c  The Block Miner  -- Child Process
 *
 * Copyright (c) 2019 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.PDF   **** NO WARRANTY ****
 *
 * The Mochimo Project System Software
 *
 * Date: 13 January 2018
 *
 * Expect this file to be if-def'd up for various miners.
 *
 */

#include <inttypes.h>
#include "peach.h"

#ifdef CUDANODE
/* trigg algo prototypes */
int trigg_init_cuda(byte difficulty, byte *blockNumber);
void trigg_free_cuda();
void trigg_generate_cuda(byte *mroot, word32 *hps, byte *runflag);
/* peach algo prototypes */
#include "algo/peach/cuda_peach.h"
#endif

uint8_t nvml_init = 0;

/* miner blockin blockout -- child process */
int miner(char *blockin, char *blockout)
{
   BTRAILER bt;
   FILE *fp;
   SHA256_CTX bctx;  /* to resume entire block hash after bcon.c */

   char *haiku;
   char phaiku[256];
   
   time_t htime;
   word32 temp[3], hcount, hps;
   static word32 v24trigger[2] = { V24TRIGGER, 0 };

#ifdef CUDANODE
   if (!nvml_init) {
      init_nvml();
      nvml_init = 1;
   }
#endif

   /* Keep a separate rand16() sequence for miner child */
   if(read_data(&temp, 12, "mseed.dat") == 12)
      srand16(temp[0], temp[1], temp[2]);

   for( ;; sleep(10)) {
      /* Running is set to 0 on SIGTERM */
      if(!Running) break;
      if(!exists(blockin)) break;
      if(read_data(&bctx, sizeof(bctx), "bctx.dat") != sizeof(bctx)) {
         error("miner: cannot read bctx.dat");
         break;
      }
      unlink("bctx.dat");
      if((fp = fopen(blockin, "rb")) == NULL) {
         error("miner: cannot open %s", blockin);
         break;
      }
      if(fseek(fp, -(sizeof(BTRAILER)), SEEK_END) != 0) {
         fclose(fp);
         error("miner: seek error");
         break;
      }
      if(fread(&bt, 1, sizeof(bt), fp) != sizeof(bt)) {
         error("miner: read error");
         fclose(fp);
         break;
      }
      fclose(fp);
      unlink("miner.tmp");
      if(rename(blockin, "miner.tmp") != 0) {
         error("miner: cannot rename %s", blockin);
         break;
      }

      show("solving");
      if(Trace)
         plog("miner: beginning solve: %s block: 0x%s", blockin,
              bnum2hex(bt.bnum));

      if(cmp64(bt.bnum, v24trigger) > 0) { /* v2.4 and later */
      
#ifdef CUDANODE
         /* Allocate and initialize necessary memory on CUDA devices */
         if (init_cuda_peach(Difficulty, bt.phash, bt.bnum) < 1) {
            error("Miner failed to initilize CUDA devices\n");
            break;
         }
         /* Run the peach cuda miner */
         cuda_peach((byte *) &bt, &hps, &Running);
         /* Free allocated memory on CUDA devices */
         free_cuda_peach();
         /* Block validation check */
         if (Running && peach(&bt, Difficulty, NULL, 1)) {
            byte* bt_bytes = (byte*) &bt;
            char hex[124 * 4];
            for(int i = 0; i < 124; i++){
               sprintf(hex + i * 4, "%03i ", bt_bytes[i]);
            }          
            error("!!!!!CUDA Peach solved block is not valid!!!!!");
            error("CPU BT -> %s", hex);
            sleep(5);
            break;
         }
         /* K all g... */
#endif
#ifdef CPUNODE
         if(peach(&bt, Difficulty, &hps, 0)) break;
#endif

      } /* end if(cmp64(bt.bnum... */


/* Legacy handler is CPU Only for all v2.3 and earlier blocks */

      if(cmp64(bt.bnum, v24trigger) <= 0)
      {
         /* Create the solution state-space beginning with
          * the first plausible link on the TRIGG chain.
          */
         trigg_solve(bt.mroot, bt.difficulty[0], bt.bnum);
         
#ifdef CUDANODE
         /* Initialize CUDA specific memory allocations
          * and check for obvious errors */
         if(trigg_init_cuda(bt.difficulty[0], bt.bnum) < 1) {
            error("Cuda initialization failed. Check nvidia-smi");
            trigg_free_cuda();
            break;
         }
         /* Run the trigg cuda miner */
         trigg_generate_cuda(bt.mroot, &hps, &Running);
         /* Free CUDA specific memory allocations */
         trigg_free_cuda();
#endif
#ifdef CPUNODE
         for(hcount = 0, htime = time(NULL); Running; hcount++)
            if(trigg_generate(bt.mroot, bt.difficulty[0]) != NULL)
               break;
         
         /* Calculate and write Haiku/s to disk */
         htime = time(NULL) - htime;
         if(htime == 0) htime = 1;
         hps = hcount / htime;
#endif

         /* Block validation check */
         if (Running && !trigg_check(bt.mroot, bt.difficulty[0], bt.bnum)) {
            printf("ERROR - Block is not valid\n");
            break;
         }
      } /* end legacy handler */

      write_data(&hps, sizeof(hps), "hps.dat");  /* unsigned int haiku per second */
      if(!Running) break;
      
      /* Print Haiku */
      trigg_expand2(bt.nonce, phaiku);
      if(!Bgflag) printf("\n%s\n\n", phaiku);

      /* Everything below this line is shared code.  */
      show("solved");

      /* solved block! */
      sleep(1);  /* make sure that stime is not to early */
      put32(bt.stime, time(NULL));  /* put solve time in trailer */
      /* hash-in nonce and solved time to hash
       * context begun by bcon.
       */
      sha256_update(&bctx, bt.nonce, HASHLEN + 4);
      sha256_final(&bctx, bt.bhash);  /* put hash in block trailer */
      fp = fopen("miner.tmp", "r+b");
      if(fp == NULL) {
         if(Trace) plog("miner: cannot re-open miner.tmp");
         break;
      }
      if(fseek(fp, -(sizeof(BTRAILER)), SEEK_END) != 0) {
         fclose(fp);
         error("miner: cannot fseek(trailer) miner.tmp");
         break;
      }
      if(fwrite(&bt, 1, sizeof(bt), fp) != sizeof(bt)) {
         fclose(fp);
         error("miner: cannot fwrite(trailer) miner.tmp");
         break;
      }
      fclose(fp);
      unlink(blockout);
      if(rename("miner.tmp", blockout) != 0) {
         error("miner: cannot rename miner.tmp");
         break;
      }

      if(Trace)
         plog("miner: solved block 0x%s is now: %s",
              bnum2hex(bt.bnum), blockout);
      break;
   }  /* end for(;;) exit miner  */

   getrand16(temp, &temp[1], &temp[2]);
   write_data(&temp, 12, "mseed.dat");    /* maintain rand16() sequence */
   printf("Miner exiting...\n");
   return 0;
}  /* end miner() */


/* Start the miner as a child process */
int start_miner(void)
{
   pid_t pid;

   if(Mpid) return VEOK;
   pid = fork();
   if(pid < 0) return VERROR;
   if(pid) { Mpid = pid; return VEOK; }  /* parent */
   /* child */
   miner("cblock.dat", "mblock.dat");
   exit(0);
}  /* end start_miner() */
