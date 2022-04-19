/* bval.c  Block Validator
 *
 * Copyright (c) 2019 by Adequate Systems, LLC.  All Rights Reserved.
 * See LICENSE.PDF   **** NO WARRANTY ****
 *
 * The Mochimo Project System Software
 *
 * Date: 8 January 2018
 *
 * NOTE: Invoked by server.c update() by wait on system()
 *
 * Returns exit code 0 on successful validation,
 *                   1 I/O errors, or
 *                   >= 2 Peerip needs pinklist().
 *
 * Inputs:  argv[1],    rblock.dat, the block to validate
 *          ledger.dat  the ledger of address balances
 *
 * Outputs: ltran.dat  transaction file to post against ledger.dat
 *          exit status 0=valid or non-zero=not valid.
 *          renames argv[1] to "vblock.dat" on good validation.
*/

/* include guard */
#ifndef MOCHIMO_BVAL_C
#define MOCHIMO_BVAL_C


/* system support */
#include <sys/types.h>
#include <sys/wait.h>

/* extended-c support */
#include "extint.h"     /* integer support */
#include "extlib.h"     /* general support */
#include "extmath.h"    /* 64-bit math support */
#include "extprint.h"   /* print/logging support */

/* crypto support */
#include "crc16.h"

/* algo support */
#include "peach.h"
#include "trigg.h"
#include "wots.h"

/* mochimo support */
#include "config.h"
#include "data.c"
#include "daemon.c"
#include "ledger.c"
#include "txval.c"  /* for mtx */
#include "util.c"

word32 Tnum = -1;    /* transaction sequence number */
char *Bvaldelfname;  /* set == argv[1] to delete input file on failure */
TXQENTRY *Q2;        /* tag mods */


void cleanup(int ecode)
{
   if(Q2 != NULL) free(Q2);
   unlink("ltran.tmp");
   if(Bvaldelfname) unlink(Bvaldelfname);
   if(Trace) plog("cleanup() with ecode %i", ecode);
   exit(1);  /* no pink-list */
}

void drop(char *message)
{
   if(Trace && message)
      plog("bval: drop(): %s TX index = %d", message, Tnum);
   cleanup(3);
}


void baddrop(char *message)
{
   if(Trace && message)
      plog("bval: baddrop(): %s from: %s  TX index = %d",
           message, ntoa(&Peerip, NULL), Tnum);
   /* add Peerip to epoch pink list */
   cleanup(3);  /* put on epink.lst */
}


void bail(char *message)
{
   if(message) perr("bval: %s", message);
   cleanup(1);
}


#if TXTAGLEN != 12
   TXTAGLEN must be 12 for tag code in bval.c
#endif


/* Invocation: bval file_to_validate */
int main(int argc, char **argv)
{
   BHEADER bh;             /* fixed length block header */
   static BTRAILER bt;     /* block trailer */
   static TXQENTRY tx;     /* Holds one transaction in the array */
   FILE *fp;               /* to read block file */
   FILE *ltfp;             /* ledger transaction output file ltran.tmp */
   word32 hdrlen, tcount;  /* header length and transaction count */
   int cond;
   static LENTRY src_le;            /* source and change ledger entries */
   word32 total[2];                 /* for 64-bit maths */
   static word8 mroot[HASHLEN];      /* computed Merkel root */
   static word8 bhash[HASHLEN];      /* computed block hash */
   static word8 tx_id[HASHLEN];      /* hash of transaction and signature */
   static word8 prev_tx_id[HASHLEN]; /* to check sort */
   static SHA256_CTX bctx;  /* to hash entire block */
   static SHA256_CTX mctx;  /* to hash transaction array */
   word32 bnum[2], stemp;
   static word32 mfees[2], mreward[2];
   unsigned long blocklen;
   int count;
   static word8 do_rename = 1;
   static word8 pk2[WOTSSIGBYTES], message[32], rnd2[32];  /* for WOTS */
   static char haiku[256];
   word32 now;
   TXQENTRY *qp1, *qp2, *qlimit;   /* tag mods */
   clock_t ticks;
   static word32 tottrigger[2] = { V23TRIGGER, 0 };
   static word32 v24trigger[2] = { V24TRIGGER, 0 };
   MTX *mtx;
   static word8 addr[TXADDRLEN];  /* for mtx scan 4 */
   int j;  /* mtx */
   static TXQENTRY txs;     /* for mtx sig check */

   /* Adding constants to skip validation on BoxingDay corrupt block
    * provided the blockhash matches.  See "Boxing Day Anomaly" write
    * up on the Wiki or on [ REDACTED ] for more details. */
   static word32 boxingday[2] = { 0x52d3c, 0 };
   static char boxdayhash[32] = {
      0x2f, 0xfa, 0xb9, 0xb9, 0x00, 0xe1, 0xbc, 0xa8,
      0x25, 0x19, 0x20, 0xc2, 0xdd, 0xf0, 0x46, 0xb8,
      0x07, 0x44, 0x2a, 0xbb, 0xfa, 0x5e, 0x94, 0x51,
      0xb0, 0x60, 0x03, 0xcc, 0x82, 0x2d, 0xb1, 0x12
   };

   ticks = clock();
   fix_signals();
   close_extra();

   if(argc < 2) {
      printf("\nusage: bval {rblock.dat | file_to_validate} [-n]\n"
             "  -n no rename, just create ltran.dat\n"
             "This program is spawned from server.c\n\n");
      exit(1);
   }

   if(sizeof(MTX) != sizeof(TXQENTRY)) bail("bad MTX size");

   if(argc > 2 && argv[2][0] == '-') {
      if(argv[2][1] == 'n') do_rename = 0;
   }

   if(strcmp(argv[1], "rblock.dat") == 0) Bvaldelfname = argv[1];
   unlink("vblock.dat");
   unlink("ltran.dat");

   /* get global block number, peer ip, etc. */
   if(read_global() != VEOK)
      bail("Cannot read_global()");

   if(Trace) set_output_file(LOGFNAME, "a");

   /* open ledger read-only */
   if(le_open("ledger.dat", "rb") != VEOK)
      bail("Cannot open ledger.dat");

   /* create ledger transaction temp file */
   ltfp = fopen("ltran.tmp", "wb");
   if(ltfp == NULL) bail("Cannot create ltran.tmp");

   /* open the block to validate */
   fp = fopen(argv[1], "rb");
   if(!fp) {
badread:
      bail("Cannot read input rblock.dat");
   }
   if(fread(&hdrlen, 1, 4, fp) != 4) goto badread;  /* read header length */
   /* regular fixed size block header */
   if(hdrlen != sizeof(BHEADER))
      drop("bad hdrlen");

   /* compute block file length */
   if(fseek(fp, 0, SEEK_END)) goto badread;
   blocklen = ftell(fp);

   /* Read block trailer:
    * Check phash, bnum,
    * difficulty, Merkel Root, nonce, solve time, and block hash.
    */
   if(fseek(fp, -(sizeof(BTRAILER)), SEEK_END)) goto badread;
   if(fread(&bt, 1, sizeof(BTRAILER), fp) != sizeof(BTRAILER))
      drop("bad trailer read");
   if(cmp64(bt.mfee, Mfee) < 0)
      drop("bad mining fee");
   if(get32(bt.difficulty) != Difficulty)
      drop("difficulty mismatch");

   /* Check block times and block number. */
   stemp = get32(bt.stime);
   /* check for early block time */
   if(stemp <= Time0) drop("E");  /* unsigned time here */
   now = time(NULL);
   if(stemp > (now + BCONFREQ)) drop("F");
   add64(Cblocknum, One, bnum);
   if(memcmp(bnum, bt.bnum, 8) != 0) drop("bad block number");
   if(cmp64(bnum, tottrigger) > 0 && Cblocknum[0] != 0xfe) {
      if((word32) (stemp - get32(bt.time0)) > BRIDGE) drop("TOT");
   }

   if(memcmp(Cblockhash, bt.phash, HASHLEN) != 0)
      drop("previous hash mismatch");

   /* check enforced delay, collect haiku from block */
   if(cmp64(bnum, v24trigger) > 0) {
      if(cmp64(bt.bnum, boxingday) == 0) { /* Boxing Day Anomaly -- Bugfix */
         if(memcmp(bt.bhash, boxdayhash, 32) != 0) {
            if (Trace) plog("bval(): Boxing Day Bugfix Block Bhash Failure");
            drop("bval(): Boxing Day Bugfix Block Bhash Failure");
         }
      } else
      if(peach_check(&bt)){
         drop("peach validation failed!");
      }
   }
   if(cmp64(bnum, v24trigger) <= 0) {
      if(trigg_check(&bt)) {
      drop("trigg_check() failed!");
      }
   }
   if(!Bgflag) printf("\n%s\n\n", trigg_expand(bt.nonce, haiku));
   /* Read block header */
   if(fseek(fp, 0, SEEK_SET)) goto badread;
   if(fread(&bh, 1, hdrlen, fp) != hdrlen)
      drop("short header read");
   get_mreward(mreward, bnum);
   if(memcmp(bh.mreward, mreward, 8) != 0)
      drop("bad mining reward");
   if(ADDR_HAS_TAG(bh.maddr))
      drop("bh.maddr has tag!");

   /* fp left at offset of Merkel Block Array--ready to fread() */

   sha256_init(&bctx);   /* begin entire block hash */
   sha256_update(&bctx, (word8 *) &bh, hdrlen);  /* ... with the header */

   if(NEWYEAR(bt.bnum)) memcpy(&mctx, &bctx, sizeof(mctx));

   /*
    * Copy transaction count from block trailer and check.
    */
   tcount = get32(bt.tcount);
   if(tcount == 0 || tcount > MAXBLTX)
      baddrop("bad bt.tcount");
   if((hdrlen + sizeof(BTRAILER) + (tcount * sizeof(TXQENTRY))) != blocklen)
      drop("bad block length");

   /* temp TX tag processing queue */
   Q2 = malloc(tcount * sizeof(TXQENTRY));
   if(Q2 == NULL) bail("no memory!");

   /* Now ready to read transactions */
   if(!NEWYEAR(bt.bnum)) sha256_init(&mctx);   /* begin Merkel Array hash */

   /* Validate each transaction */
   for(Tnum = 0; Tnum < tcount; Tnum++) {
      if(Tnum >= MAXBLTX)
         drop("too many TX's");
      if(fread(&tx, 1, sizeof(TXQENTRY), fp) != sizeof(TXQENTRY))
         drop("bad TX read");
      if(memcmp(tx.src_addr, tx.chg_addr, TXADDRLEN) == 0)
         drop("src == chg");
      if(!TX_IS_MTX(&tx) && memcmp(tx.src_addr, tx.dst_addr, TXADDRLEN) == 0)
         drop("src == dst");

      if(cmp64(tx.tx_fee, Mfee) < 0) drop("tx_fee is bad");

      /* running block hash */
      sha256_update(&bctx, (word8 *) &tx, sizeof(TXQENTRY));
      /* running Merkel hash */
      sha256_update(&mctx, (word8 *) &tx, sizeof(TXQENTRY));
      /* tx_id is hash of tx.src_add */
      sha256(tx.src_addr, TXADDRLEN, tx_id);
      if(memcmp(tx_id, tx.tx_id, HASHLEN) != 0)
         drop("bad TX_ID");

      /* Check that tx_id is sorted. */
      if(Tnum != 0) {
         cond = memcmp(tx_id, prev_tx_id, HASHLEN);
         if(cond < 0)  drop("TX_ID unsorted");
         if(cond == 0) drop("duplicate TX_ID");
      }
      /* remember this tx_id for next time */
      memcpy(prev_tx_id, tx_id, HASHLEN);

      /* check WTOS signature */
      if(TX_IS_MTX(&tx) && get32(Cblocknum) >= MTXTRIGGER) {
         memcpy(&txs, &tx, sizeof(txs));
         mtx = (MTX *) &txs;
         memset(mtx->zeros, 0, MDST_NUM_DZEROS);  /* always signed when zero */
         sha256(txs.src_addr, TRANSIGHASHLEN, message);
      } else {
         sha256(tx.src_addr, TRANSIGHASHLEN, message);
      }
      memcpy(rnd2, &tx.src_addr[TXSIGLEN+32], 32);  /* copy WOTS addr[] */
      wots_pk_from_sig(pk2, tx.tx_sig, message, &tx.src_addr[TXSIGLEN],
                       (word32 *) rnd2);
      if(memcmp(pk2, tx.src_addr, TXSIGLEN) != 0)
         baddrop("WOTS signature failed!");

      /* look up source address in ledger */
      if(le_find(tx.src_addr, &src_le, NULL, TXADDRLEN) == FALSE)
         drop("src_addr not in ledger");

      total[0] = total[1] = 0;
      /* use add64() to check for carry out */
      cond =  add64(tx.send_total, tx.change_total, total);
      cond += add64(tx.tx_fee, total, total);
      if(cond) drop("total overflow");

      if(cmp64(src_le.balance, total) != 0)
         drop("bad transaction total");
      if(!TX_IS_MTX(&tx)) {
         if(tag_valid(tx.src_addr, tx.chg_addr, tx.dst_addr, bt.bnum)
            != VEOK) drop("tag not valid");
      } else {
         if(mtx_val((MTX *) &tx, Mfee) != 0) drop("bad mtx_val()");
      }

      memcpy(&Q2[Tnum], &tx, sizeof(TXQENTRY));  /* copy TX to tag queue */

      if(add64(mfees, tx.tx_fee, mfees)) {
fee_overflow:
         bail("mfees overflow");
      }
   }  /* end for Tnum */
   if(NEWYEAR(bt.bnum))
      /* phash, bnum, mfee, tcount, time0, difficulty */
      sha256_update(&mctx, (word8 *) &bt, (HASHLEN+8+8+4+4+4));

   sha256_final(&mctx, mroot);  /* compute Merkel Root */
   if(memcmp(bt.mroot, mroot, HASHLEN) != 0)
      baddrop("bad Merkle root");

   sha256_update(&bctx, (word8 *) &bt, sizeof(BTRAILER) - HASHLEN);
   sha256_final(&bctx, bhash);
   if(memcmp(bt.bhash, bhash, HASHLEN) != 0)
      drop("bad block hash");

   /* tag search  Begin ... */
   qlimit = &Q2[tcount];
   for(qp1 = Q2; qp1 < qlimit; qp1++) {
      if(!ADDR_HAS_TAG(qp1->src_addr)
         || memcmp(ADDR_TAG_PTR(qp1->src_addr), ADDR_TAG_PTR(qp1->chg_addr),
                   TXTAGLEN) != 0) continue;
      /* Step 2: Start another big-O n squared, nested loop here... */
      for(qp2 = Q2; qp2 < qlimit; qp2++) {
         if(qp1 == qp2) continue;  /* added -trg */
         if(TX_IS_MTX(qp2)) continue;  /* skip multi-dst's for now */
         /* if src1 == dst2, then copy chg1 to dst2 -- 32-bit for DSL -trg */
         if(   *((word32 *) ADDR_TAG_PTR(qp1->src_addr))
            == *((word32 *) ADDR_TAG_PTR(qp2->dst_addr))
            && *((word32 *) (ADDR_TAG_PTR(qp1->src_addr) + 4))
            == *((word32 *) (ADDR_TAG_PTR(qp2->dst_addr) + 4))
            && *((word32 *) (ADDR_TAG_PTR(qp1->src_addr) + 8))
            == *((word32 *) (ADDR_TAG_PTR(qp2->dst_addr) + 8)))
                   memcpy(qp2->dst_addr, qp1->chg_addr, TXADDRLEN);
      }  /* end for qp2 */
   }  /* end for qp1 */

   /*
    * Three times is the charm...
    */
   for(Tnum = 0, qp1 = Q2; qp1 < qlimit; qp1++, Tnum++) {
      /* Re-do all the maths again... */
      total[0] = total[1] = 0;
      cond =  add64(qp1->send_total, qp1->change_total, total);
      cond += add64(qp1->tx_fee, total, total);
      if(cond) bail("scan3 total overflow");

      /* Write ledger transactions to ltran.tmp for all src and chg,
       * but only non-mtx dst
       * that will have to be sorted, read again, and applied by bup...
       */
      fwrite(qp1->src_addr,  1, TXADDRLEN, ltfp);
      fwrite("-",            1,         1, ltfp);  /* debit src addr */
      fwrite(total,          1,         8, ltfp);
      /* add to or create non-multi dst address */
      if(!TX_IS_MTX(qp1) && !iszero(qp1->send_total, 8)) {
         fwrite(qp1->dst_addr,   1, TXADDRLEN, ltfp);
         fwrite("A",             1,         1, ltfp);
         fwrite(qp1->send_total, 1,         8, ltfp);
      }
      /* add to or create change address */
      if(!iszero(qp1->change_total, 8)) {
         fwrite(qp1->chg_addr,     1, TXADDRLEN, ltfp);
         fwrite("A",               1,         1, ltfp);
         fwrite(qp1->change_total, 1,         8, ltfp);
      }
   }  /* end for Tnum -- scan 3 */


   if(Tnum != tcount) bail("scan 3");
   /* mtx tag search  Begin scan 4 ...
    *
    * Write out the multi-dst trans using tag scan logic @
    * that more or less repeats the above big-O n-squared loops, and
    * expands the tags, and copies addresses around.
    */
   for(qp1 = Q2; qp1 < qlimit; qp1++) {
      if(!TX_IS_MTX(qp1)) continue;  /* only multi-dst's this time */
      mtx = (MTX *) qp1;  /* poor man's union */
      /* For each dst[] tag... */
      for(j = 0; j < MDST_NUM_DST; j++) {
         if(iszero(mtx->dst[j].tag, TXTAGLEN)) break; /* end of dst[] */
         memcpy(ADDR_TAG_PTR(addr), mtx->dst[j].tag, TXTAGLEN);
         /* If dst[j] tag not found, write money back to chg addr. */
         if(tag_find(addr, addr, NULL, TXTAGLEN) != VEOK) {
            count =  fwrite(mtx->chg_addr, TXADDRLEN, 1, ltfp);
            count += fwrite("A", 1, 1, ltfp);
            count += fwrite(mtx->dst[j].amount, 8, 1, ltfp);
            if(count != 3) bail("bad I/O dst-->chg write");
            continue;  /* next dst[j] */
         }
         /* Start another big-O n-squared, nested loop here... scan 5 */
         for(qp2 = Q2; qp2 < qlimit; qp2++) {
            if(qp1 == qp2) continue;
            /* if dst[j] tag == any other src addr tag and chg addr tag,
             * copy other chg addr to dst[] addr.
             */
            if(!ADDR_HAS_TAG(qp2->src_addr)) continue;
            if(memcmp(ADDR_TAG_PTR(qp2->src_addr),
                      ADDR_TAG_PTR(qp2->chg_addr), TXTAGLEN) != 0)
                         continue;
            if(memcmp(ADDR_TAG_PTR(qp2->src_addr), ADDR_TAG_PTR(addr),
                      TXTAGLEN) == 0) {
                         memcpy(addr, qp2->chg_addr, TXADDRLEN);
                         break;
            }
         }  /* end for qp2 scan 5 */
         /* write out the dst transaction */
         count =  fwrite(addr, TXADDRLEN, 1, ltfp);
         count += fwrite("A", 1, 1, ltfp);
         count += fwrite(mtx->dst[j].amount, 8, 1, ltfp);
         if(count != 3) bail("bad I/O scan 4");
      }  /* end for j */
   }  /* end for qp1 */
   /* end mtx scan 4 */

   /* Create a transaction amount = mreward + mfees
    * address = bh.maddr
    */
   if(add64(mfees, mreward, mfees)) goto fee_overflow;
   /* Make ledger tran to add to or create mining address.
    * '...Money from nothing...'
    */
   count =  fwrite(bh.maddr, 1, TXADDRLEN, ltfp);
   count += fwrite("A",      1,         1, ltfp);
   count += fwrite(mfees,    1,         8, ltfp);
   if(count != (TXADDRLEN+1+8) || ferror(ltfp))
      bail("ltfp I/O error");

   free(Q2);  Q2 = NULL;
   le_close();
   fclose(ltfp);
   fclose(fp);
   rename("ltran.tmp", "ltran.dat");
   unlink("vblock.dat");
   if(do_rename)
      rename(argv[1], "vblock.dat");
   if(Trace)
      plog("bval: block validated to vblock.dat (%u usec.)",
           (word32) (clock() - ticks));
   if(argc > 2) printf("Validated\n");
   return 0;  /* success */
}  /* end main() */

/* end include guard */
#endif
