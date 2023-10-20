/**
 * @private
 * @headerfile ledger.h <ledger.h>
 * @copyright Adequate Systems LLC, 2018-2022. All Rights Reserved.
 * <br />For license information, please refer to ../LICENSE.md
*/

/* include guard */
#ifndef MOCHIMO_LEDGER_C
#define MOCHIMO_LEDGER_C


#include "ledger.h"

/* internal support */
#include "util.h"
#include "tag.h"
#include "sort.h"
#include "global.h"

/* external support */
#include <string.h>
#include "sha256.h"
#include "extprint.h"
#include "extmath.h"
#include "extlib.h"
#include <errno.h>

static FILE *Lefp;
static unsigned long Nledger;
word32 Sanctuary;
word32 Lastday;

/**
 * Hashed-based address comparison function. Includes tag in comparison.
 * @param a Pointer to data to compare
 * @param b Pointer to data to compare against
 * @return (int) value representing comparison result
 * @retval 0 @a a is equal to @a b
 * @retval <0 @a a is less than @a b
 * @retval >0 @a a is greater than @a b
*/
static int addr_compare(const void *a, const void *b)
{
   return memcmp(a, b, TXADDRLEN);
}

/**
 * WOTS+ address comparison function. Includes tag in comparison.
 * @param a Pointer to data to compare
 * @param b Pointer to data to compare against
 * @return (int) value representing comparison result
 * @retval 0 @a a is equal to @a b
 * @retval <0 @a a is less than @a b
 * @retval >0 @a a is greater than @a b
*/
static int addr_compare_wots(const void *a, const void *b)
{
   return memcmp(a, b, TXWOTSLEN);
}

/**
 * Convert a WOTS+ address to a Hashed-based address. Copies tag data.
 * @param hash Pointer to destination hash-based address
 * @param wots Pointer to source WOTS+ address
*/
void hash_wots_addr(void *hash, const void *wots)
{
   sha256(wots, TXSIGLEN, hash);
   memcpy(
      (unsigned char *) hash + (TXADDRLEN - TXTAGLEN),
      (unsigned char *) wots + (TXWOTSLEN - TXTAGLEN),
      TXTAGLEN
   );
}

/**
 * NOTE: imported from extended-c v2.0.0-alpha.2 extlib.c
 * Sort a file containing @a size length elements. If file data fits into
 * the memory buffer, @a bufsz, data is simply sorted in-memory with quick
 * sort. Otherwise, an external merge sort algorithm is applied.
 * @param filename Name of file to sort
 * @param size Size of each element in file
 * @param bufsz Size of the buffer of each run used for in-memory sorting
 * @param comp Comparison function to use when sorting elements
 * @returns 0 on success, or non-zero on error. Check errno for details.
 * @exception errno=EINVAL A function parameter is invalid
*/
static int filesort(const char *filename, size_t size, size_t blocksz,
   int (*comp)(const void *, const void *))
{
   void *a, *b, *buffer;
   FILE *afp, *bfp, *ofp;
   long long aidx, bidx;
   long long filelen, block;
   long long start, mid, end;
   size_t filecount, count, in;
   int cond;
   char fname[FILENAME_MAX];

   /* sanity checks */
   if (filename == NULL || comp == NULL) goto FAIL_INVAL;
   if (size == 0 || blocksz == 0) goto FAIL_INVAL;

   /* PHASE 1: pre-sort blocks of data */

   /* get count for blocksz (adjust) */
   count = blocksz / size;
   blocksz = count * size;
   /* create buffer, open input/output files */
   ofp = fopen(filename, "rb+");
   buffer = malloc(blocksz);
   /* check failures */
   if (ofp == NULL || buffer == NULL) goto FAIL1;

   /* get filelen */
   if (fseek64(ofp, 0LL, SEEK_END) != 0) goto FAIL1;
   if ((filelen = ftell64(ofp)) == EOF) goto FAIL1;
   filecount = (size_t) (filelen / size);

   for (rewind(ofp); filecount > 0; filecount -= in) {
      /* read input file in chunks for presort */
      if (filecount < count) count = filecount;
      in = fread(buffer, size, count, ofp);
      if (in < count && ferror(ofp)) goto FAIL1;
      if (fseek(ofp, -(in * size), SEEK_CUR) != 0) goto FAIL1;
      /* check data was read */
      if (in > 0) {
         /* perform sort on buffer data, write to output */
         if (in > 1) qsort(buffer, in, size, comp);
         if (fwrite(buffer, size, in, ofp) != in) goto FAIL1;
      }
   }
   /* cleanup */
   fclose(ofp);
   free(buffer);

   /* PHASE 2: merge sort blocks together until nothing left to sort */

   /* obtain file size */
   filelen = EOF;
   ofp = fopen(filename, "rb");
   if (ofp == NULL) return (-1);
   if (fseek64(ofp, 0LL, SEEK_END) == 0) filelen = ftell64(ofp);
   fclose(ofp);
   /* check filesize */
   if (filelen == EOF) return (-1);

   /* create comparison buffers */
   a = malloc(size);
   b = malloc(size);
   if (a == NULL || b == NULL) goto FAIL2;

   snprintf(fname, FILENAME_MAX, "%s.sort", filename);

   /* iterate until (sorted) block size is greater than total filesize */
   for (block = (long long) blocksz; block < filelen; block <<= 1) {
      /* open files for merge sorting */
      afp = fopen(filename, "rb");
      bfp = fopen(filename, "rb");
      ofp = fopen(fname, "wb");
      if (afp == NULL || bfp == NULL || ofp == NULL) goto FAIL2_IO;
      /* iterate over every "block pair", shifting end to start */
      for (start = 0; start < filelen; start = end) {
         /* set index parameters */
         mid = start + block;
         end = mid + block;
         if (mid > filelen) mid = end = filelen;
         else if (end > filelen) end = filelen;
         aidx = start;
         bidx = mid;

         /* pre-read first values into buffers */
         if (fseek64(afp, aidx, SEEK_SET) != 0) goto FAIL2_IO;
         if (fread(a, size, 1, afp) != 1) goto FAIL2_IO;
         if (bidx < end) {
            if (fseek64(bfp, bidx, SEEK_SET) != 0) goto FAIL2_IO;
            if (fread(b, size, 1, bfp) != 1) goto FAIL2_IO;
         }
         /* walk the block pair until data is (merge) sorted */
         while (aidx < mid || bidx < end) {
            if (aidx >= mid) cond = 1;
            else if (bidx >= end) cond = -1;
            else cond = comp(a, b);
            /* determine comparison result */
            if (cond <= 0) {
               /* write a to output and read another (if available) */
               if (fwrite(a, size, 1, ofp) != 1) goto FAIL2_IO;
               aidx += size;
               if (aidx < mid) {
                  if (fread(a, size, 1, afp) != 1) goto FAIL2_IO;
               }
            } else {
               /* write b to output and read another (if available) */
               if (fwrite(b, size, 1, ofp) != 1) goto FAIL2_IO;
               bidx += size;
               if (bidx < end) {
                  if (fread(b, size, 1, bfp) != 1) goto FAIL2_IO;
               }
            }
         }
      }
      /* close files and move result back to filename */
      fclose(ofp);
      fclose(bfp);
      fclose(afp);
      if (remove(filename) != 0) goto FAIL2;
      if (rename(fname, filename) != 0) goto FAIL2;
   }
   /* free buffers */
   free(b);
   free(a);

   /* sort success */
   return 0;

/* error handling */
FAIL_INVAL:
   set_errno(EINVAL);
   return (-1);
FAIL2_IO:
   if (ofp) fclose(ofp);
   if (bfp) fclose(bfp);
   if (afp) fclose(afp);
FAIL2:
   if (b) free(b);
   if (a) free(a);
   return (-1);
FAIL1:
   if (buffer) free(buffer);
   if (ofp) fclose(ofp);
   return (-1);
}  /* end filesort() */

/* Open ledger "ledger.dat" */
int le_open(char *ledger, char *fopenmode)
{
   unsigned long offset;

   /* Already open? */
   if(Lefp) return VEOK;
   Nledger = 0;
   Lefp = fopen(ledger, fopenmode);
   if(Lefp == NULL)
      return perrno(errno, "le_open(): Cannot open ledger");
   if(fseek(Lefp, 0, SEEK_END)) goto bad;
   offset = ftell(Lefp);
   if(offset < sizeof(LENTRY) || (offset % sizeof(LENTRY)) != 0) goto bad;
   Nledger = offset / sizeof(LENTRY);  /* number of ledger entries */
   return VEOK;
bad:
   fclose(Lefp);
   Lefp = NULL;
   return perr("le_open(): Bad ledger I/O format");
}  /* end le_open() */


void le_close(void)
{
   if(Lefp == NULL) return;
   fclose(Lefp);
   Lefp = NULL;
   Nledger = 0;
}

/**
 * Binary search for ledger address. If found, le is filled with the found
 * ledger entry data. Hash-based addresses are derived from supplied WOTS+
 * addresses where an appropriate length parameter is provided.
 * @param addr Address data to search for
 * @param le Pointer to place found ledger entry
 * @param len Length of address data to search
 * @return (int) value representing found result
 * @retval 0 on not found; check errno for details
 * @retval 1 on found; check le pointer for ledger data
*/
int le_find(word8 *addr, LENTRY *le, word16 len)
{
   long cond, mid, hi, low;
   word8 addr2[TXADDRLEN];

   if(Lefp == NULL) {
      perr("le_find(): use le_open() first!");
      return 0;
   }

   /* derive hash-based address from incompatible address lengths */
   switch (len) {
      /* support for TXSIGLEN or old balance request with ZEROED Tag */
      case TXSIGLEN: /* fallthrough */
      case (TXWOTSLEN - TXTAGLEN):
         /* derive hash-based address from WOTS+ address (partial) */
         sha256(addr, TXSIGLEN, addr2);
         len = TXADDRLEN - TXTAGLEN;
         break;
      /* support for full WOTS+ address hash (inc. tag) */
      case TXWOTSLEN:
         hash_wots_addr(addr2, addr);
         len = TXADDRLEN;
         break;
      /* otherwise, assume hash-based address from input */
      default:
         memcpy(addr2, addr, (size_t) len);
         /* search length protection */
         if (len > TXADDRLEN) {
            len = TXADDRLEN;
         }
   }

   low = 0;
   hi = Nledger - 1;

   while(low <= hi) {
      mid = (hi + low) / 2;
      if(fseek(Lefp, mid * sizeof(LENTRY), SEEK_SET) != 0)
         { perr("le_find(): fseek");  break; }
      if(fread(le, 1, sizeof(LENTRY), Lefp) != sizeof(LENTRY))
         { perrno(errno, "le_find(): fread");  break; }
      cond = memcmp(addr2, le->addr, (size_t) len);
      if(cond == 0) return 1;  /* found target addr */
      if(cond < 0) hi = mid - 1; else low = mid + 1;
   }  /* end while */

   return 0;  /* not found */
}  /* end le_find() */

/**
 * Extract a hashed-based ledger from a neo-genesis block. Checks sort.
 * Compatible with both WOTS+ and Hashed-based neo-genesis blocks.
 * @param ngfname Filename of the neo-genesis block
 * @return (int) value representing extraction result
 * @retval VEBAD on invalid; check errno for details
 * @retval VERROR on error; check errno for details
 * @retval VEOK on success
*/
int le_extract(const char *neogen_file, const char *ledger_file)
{
   LENTRY le;              /* buffer for Hashed ledger entries */
   LENTRY_W lew;           /* buffer for WOTS+ ledger entries */
   NGHEADER ngh;           /* buffer for neo-genesis header */
   FILE *fp, *lfp;         /* FILE pointers */
   long long remain;
   word32 hdrlen;          /* buffer for block header length */
   word8 paddr[TXWOTSLEN]; /* ledger address sort check */
   int first;

   /* open files */
   fp = fopen(neogen_file, "rb");
   lfp = fopen(ledger_file, "wb");
   if (fp == NULL || lfp == NULL) goto FAIL_IO;

   /* read neo-genesis header and hdrlen value -- check ledger type */
   if (fread(&ngh, sizeof(ngh), 1, fp) != 1) goto FAIL_IO;
   hdrlen = get32(ngh.hdrlen);

   /* check header length for hash-based ledger type */
   if (hdrlen == sizeof(ngh)) {
      /* seek to start of Hashed ledger */
      if (fseek(fp, (long) hdrlen, SEEK_SET)) goto FAIL_IO;
      /* check ledger size and alignment */
      put64(&remain, ngh.lbytes);
      if (remain == 0 || remain % sizeof(le)) goto FAIL_HDRLEN;

      /* read the ledger from fp, copy it to lfp */
      for (first = 1; remain > 0LL; remain -= sizeof(le), first = 0) {
         if (fread(&le, sizeof(le), 1, fp) != 1) goto FAIL_IO;
         /* check ledger sort */
         if (!first && addr_compare(le.addr, paddr) <= 0) goto FAIL_SORT;
         memcpy(paddr, le.addr, sizeof(le.addr));
         /* write hashed ledger entries to ledger file */
         if (fwrite(&le, sizeof(le), 1, lfp) != 1) goto FAIL_IO;
      }  /* end for() */

      /* close files */
      fclose(lfp);
      fclose(fp);

      /* ledger extracted */
      return VEOK;
   }

   /* assume LEGACY ledger type */

   /* seek to start of WOTS+ ledger */
   if (fseek(fp, (long) sizeof(hdrlen), SEEK_SET)) goto FAIL_IO;
   /* check ledger size and alignment */
   remain = (long long) hdrlen - sizeof(hdrlen);
   if (remain == 0 || remain % sizeof(lew)) goto FAIL_HDRLEN;

   /* convert the WOTS+ ledger from fp to hashed, and copy it to lfp */
   for (first = 1; remain > 0LL; remain -= sizeof(lew), first = 0) {
      if (fread(&lew, sizeof(lew), 1, fp) != 1) goto FAIL_IO;
      /* check ledger sort */
      if (!first && addr_compare_wots(lew.addr, paddr) <= 0) goto FAIL_SORT;
      memcpy(paddr, lew.addr, sizeof(lew.addr));
      /* convert WOTS+ to Hashed address */
      hash_wots_addr(le.addr, lew.addr);
      put64(le.balance, lew.balance);
      /* zero fill new ledger entry zcf parameters */
      memset(le.zcf_dst, 0, sizeof(le.zcf_dst));
      memset(le.zcf_ttl, 0, sizeof(le.zcf_ttl));
      /* write hashed ledger entries to ledger file */
      if (fwrite(&le, sizeof(le), 1, lfp) != 1) goto FAIL_IO;
   }  /* end for() */

   /* close files */
   fclose(lfp);
   fclose(fp);

   /* ledger output requires a sort pass, due to WOTS+ conversion */
   /* return filesort(ledger_file, sizeof(le), LEBUFSZ, addr_compare); */
   if (filesort(ledger_file, sizeof(le), LEBUFSZ, addr_compare)) goto FAIL_IO;
   return VEOK;

/* error handling */
FAIL_SORT: set_errno(EMCM_LE_SORT); goto FAIL;
FAIL_HDRLEN: set_errno(EMCM_HDRLEN); goto FAIL;
FAIL_IO: perrno(errno, "le_extract() IO FAILURE");
FAIL:
   if (lfp) fclose(lfp);
   if (fp) fclose(fp);
   return VERROR;
}  /* end le_extract() */

/* Returns 0 on success, else error code. */
int le_renew(void)
{
   FILE *fp, *fpout;
   LENTRY le;
   int message = 0;
   word32 n, m;
   static word32 sanctuary[2];

   if(Sanctuary == 0) return 0;  /* success */
   le_close();  /* make sure ledger.dat is closed */
   plog("Lastday 0x%0x.  Carousel begins...", Lastday);
   n = m = 0;
   fp = fpout = NULL;
   sanctuary[0] = Sanctuary;

   fp = fopen("ledger.dat", "rb");
   if(fp == NULL) BAIL(1);
   fpout = fopen("ledger.tmp", "wb");
   if(fpout == NULL) BAIL(2);
   for(;;) {
      if(fread(&le, sizeof(le), 1, fp) != 1) break;
      n++;
      if(sub64(le.balance, sanctuary, le.balance)) continue;
      if(cmp64(le.balance, Mfee) <= 0) continue;
      if(fwrite(&le, sizeof(le), 1, fpout) != 1) BAIL(3);
      m++;
   }
   fclose(fp);
   fclose(fpout);
   fp = fpout = NULL;
   remove("ledger.dat");
   if(rename("ledger.tmp", "ledger.dat")) BAIL(4);
   plog("%u citizens renewed out of %u", n - m, n);
   return 0;  /* success */
bail:
   if(fp != NULL) fclose(fp);
   if(fpout != NULL) fclose(fpout);
   perr("Carousel renewal code: %d (%u,%u)", message, n - m, n);
   return message;
}  /* end le_renew() */

/**
 * Remove bad TX's from a txclean file based on the ledger file.
 * Uses "ledger.dat" as (input) ledger file, "txq.tmp" as temporary (output)
 * txclean file and renames to "txclean.dat" on success.
 * @returns VEOK on success, else VERROR
 * @note Nothing else should be using the ledger.
 * @note Leaves ledger.dat open on return.
*/
int le_txclean(void)
{
   TXQENTRY tx;            /* Holds one transaction in the array */
   LENTRY src_le;          /* for le_find() */
   FILE *fp, *fpout;       /* input/output txclean file pointers */
   MTX *mtx;               /* for MTX checks */
   word32 j;               /* unsigned iteration and comparison */
   word32 total[2];
   word32 nout, tnum; /* transaction record counters */
   word8 addr[TXWOTSLEN];  /* for tag_find() (MTX checks) */
   clock_t ticks;
   int ecode;

   /* init */
   ticks = clock();
   ecode = VEOK;
   nout = 0;
   tnum = 0;

   /* check txclean exists AND has transactions to clean */
   if (!fexists("txclean.dat")) {
      pdebug("le_txclean(): nothing to clean, done...");
      return VEOK;
   }

   /* ensure ledger is open */
   if (le_open("ledger.dat", "rb") != VEOK) {
      mError(FAIL, "le_txclean(): failed to le_open(ledger.dat)");
   }

   /* open clean TX queue and new (temp) clean TX queue */
   fp = fopen("txclean.dat", "rb");
   if (fp == NULL) mErrno(FAIL, "le_txclean(): cannot open txclean");
   fpout = fopen("txq.tmp", "wb");
   if (fpout == NULL) mErrno(FAIL2, "le_txclean(): cannot open txq");

   /* read TX from txclean.dat and process */
   for(; fread(&tx, sizeof(TXQENTRY), 1, fp); tnum++) {
      /* check src in ledger, balances and amounts are good */
      if (le_find(tx.src_addr, &src_le, TXWOTSLEN) == FALSE) {
         pdebug("le_txclean(): le_find, drop %s...", addr2str(tx.tx_id));
         continue;
      } else if (cmp64(tx.tx_fee, Myfee) < 0) {
         pdebug("le_txclean(): tx_fee, drop %s...", addr2str(tx.tx_id));
         continue;
      } else if (add64(tx.send_total, tx.change_total, total)) {
         pdebug("le_txclean(): amounts, drop %s...", addr2str(tx.tx_id));
         continue;
      } else if (add64(tx.tx_fee, total, total)) {
         pdebug("le_txclean(): total, drop %s...", addr2str(tx.tx_id));
         continue;
      } else if (cmp64(src_le.balance, total) != 0) {
         pdebug("le_txclean(): balance, drop %s...", addr2str(tx.tx_id));
         continue;
      } else if (TX_IS_MTX(&tx) && get32(Cblocknum) >= MTXTRIGGER) {
         pdebug("le_txclean(): MTX detected...");
         mtx = (MTX *) &tx;
         for(j = 0; j < MDST_NUM_DST; j++) {
            if (iszero(mtx->dst[j].tag, TXTAGLEN)) break;
            memcpy(ADDR_TAG_PTR(addr), mtx->dst[j].tag, TXTAGLEN);
            mtx->zeros[j] = 0;
            /* If dst[j] tag not found, put error code in zeros[] array. */
            if (tag_find(addr, NULL, NULL, TXTAGLEN) != VEOK) {
               mtx->zeros[j] = 1;
            }
         }
      } else if (tag_valid(tx.src_addr, tx.chg_addr, tx.dst_addr,
            NULL) != VEOK) {
         pdebug("le_txclean(): tags, drop %s...", addr2str(tx.tx_id));
         continue;
      }
      /* write TX to new queue */
      if (fwrite(&tx, sizeof(TXQENTRY), 1, fpout) != 1) {
         mError(FAIL_TX, "le_txclean(): failed to fwrite(tx): TX#%u", tnum);
      }
      nout++;
   }  /* end for (nout = tnum = 0... */

   /* cleanup / error handling */
FAIL_TX:
   fclose(fpout);
FAIL2:
   fclose(fp);
FAIL:

   /* if no failures */
   if (ecode == VEOK) {
      remove("txclean.dat");
      if (nout == 0) pdebug("le_txclean(): txclean.dat is empty");
      else if (rename("txq.tmp", "txclean.dat") != VEOK) {
         mError(FAIL, "le_txclean(): failed to move txq to txclean");
      }

      /* clean TX queue is updated */
      pdebug("le_txclean(): wrote %u/%u entries to txclean", nout, tnum);
      pdebug("le_txclean(): done in %gs", diffclocktime(clock(), ticks));
   }

   /* final cleanup */
   remove("txq.tmp");

   return ecode;
}  /* end le_txclean() */

/**
 * Update leadger by applying ledger transaction deltas to a ledger. Uses
 * "ltran.dat" as (input) ledger transaction deltas file, "ledger.tmp" as
 * temporary (output) ledger file and renames to "ledger.dat" on success.
 * Ledger file is kept sorted on addr. Ledger transaction file is sorted by
 * sortlt() on addr+trancode, where '-' comes before 'A'.
 * @returns VEOK on success, else VERROR
*/
int le_update(void)
{
   LENTRY oldle, newle;    /* input/output ledger entries */
   LTRAN lt;               /* ledger transaction  */
   FILE *ltfp, *fp;        /* input ltran and output ledger pointers */
   FILE *lefp;             /* ledger file pointers */
   clock_t ticks;
   word32 nout;            /* temp file output counter */
   word8 hold;             /* hold ledger entry for next loop */
   word8 taddr[TXADDRLEN];    /* for transaction address hold */
   word8 le_prev[TXADDRLEN];  /* for ledger sequence check */
   word8 lt_prev[TXADDRLEN];  /* for tran delta sequence check */
   int cond, ecode;

   /* init */
   ticks = clock();
   nout = 0;         /* output record counter */
   hold = 0;         /* hold ledger flag */
   memset(le_prev, 0, TXADDRLEN);
   memset(lt_prev, 0, TXADDRLEN);

   /* ensure ledger reference is closed for update */
   le_close();

   /* sort the ledger transaction file */
   if (sortlt("ltran.dat") != VEOK) {
      mError(FAIL, "le_update: bad sortlt(ltran.dat)");
   }

   /* open ledger (local ref), ledger transactions, and new ledger */
   lefp = fopen("ledger.dat", "rb");
   if (lefp == NULL) {
      mErrno(FAIL, "le_update(): failed to fopen(ledger.dat)");
   }
   ltfp = fopen("ltran.dat", "rb");
   if (ltfp == NULL) {
      mErrno(FAIL_IN, "le_update(): failed to fopen(ltran.dat)");
   }
   fp = fopen("ledger.tmp", "wb");
   if (fp == NULL) {
      mErrno(FAIL_OUT, "le_update(): failed to fopen(ledger.tmp)");
   }

   /* prepare initial ledger transaction */
   fread(&lt, sizeof(LTRAN), 1, ltfp);
   if (ferror(ltfp)) {
      mErrno(FAIL_IO, "le_update(): failed to fread(lt)");
   }

   /* while one of the files is still open */
   while (feof(lefp) == 0 || feof(ltfp) == 0) {
      /* if ledger entry on hold, skip read, else do read and sort checks */
      if (hold) hold = 0;
      else if (feof(lefp) == 0) {
         /* read ledger entry, check sort, and store entry in le_prev */
         if (fread(&oldle, sizeof(LENTRY), 1, lefp) != 1) {
            /* check file errors, else "continue" loop for eof check */
            if (ferror(lefp)) {
               mErrno(FAIL_IO, "le_update(): fread(oldle)");
            } else continue;
         } else if (memcmp(oldle.addr, le_prev, TXADDRLEN) < 0) {
            mError(FAIL_IO, "le_update(): bad ledger.dat sort");
         } else memcpy(le_prev, oldle.addr, TXADDRLEN);
      }
      /* compare ledger address to latest transaction address */
      cond = memcmp(oldle.addr, lt.addr, TXADDRLEN);
      if (cond == 0 && feof(ltfp) == 0 && feof(lefp) == 0) {
         /* If ledger and transaction addr match,
          * and both files not at end...
          * copy the old ledger entry to a new struct for editing */
         pdebug("le_update(): editing address %s...", addr2str(lt.addr));
         memcpy(&newle, &oldle, sizeof(LENTRY));
      } else if ((cond < 0 || feof(ltfp)) && feof(lefp) == 0) {
         /* If ledger compares "before" transaction or transaction eof,
          * and ledger file is NOT at end...
          * write the old ledger entry to temp file */
         if (fwrite(&oldle, sizeof(LENTRY), 1, fp) != 1) {
            mError(FAIL_IO, "le_update(): bad write on temp ledger");
         }
         nout++;  /* count records in temp file */
         continue;  /* nothing else to do */
      } else if((cond > 0 || feof(lefp)) && feof(ltfp) == 0) {
         /* If ledger compares "after" transaction or ledger eof,
          * and transaction file is NOT at end...
          */
         if(lt.trancode[0] != 'A') {
            mEdrop(FAIL_IO, "le_update(): create tran not 'A'");
         }
         pdebug("le_update(): creating address %s...", addr2str(lt.addr));
         /* CREATE NEW ADDR
          * Copy address from transaction to new ledger entry.
          */
         memcpy(&newle.addr, lt.addr, TXADDRLEN);
         memset(newle.balance, 0, 8);  /* but zero balance for apply_tran */
         /* Hold old ledger entry to insert before this addition. */
         hold = 1;
      }

      /* save ledger transaction address */
      memcpy(taddr, lt.addr, TXADDRLEN);

      do {
         pdebug("le_update(): Applying '%c' to %s...",
            (char) lt.trancode[0], addr2str(lt.addr));
         /* '-' transaction sorts before 'A' */
         if (lt.trancode[0] == 'A') {
            if (add64(newle.balance, lt.amount, newle.balance)) {
               pdebug("le_update(): balance OVERFLOW! Zero-ing balance...");
               memset(newle.balance, 0, 8);
            }
         } else if(lt.trancode[0] == '-') {
            if (cmp64(newle.balance, lt.amount) != 0) {
               mEdrop(FAIL_IO, "le_update(): '-' balance != trans amount");
            }
            memset(newle.balance, 0, 8);
         } else mError(FAIL_IO, "le_update(): bad trancode");
         /* --- ^ shouldn't happen */
         /* read next transaction */
         pdebug("le_update(): apply -- reading transaction");
         if (fread(&lt, sizeof(LTRAN), 1, ltfp) != 1) {
            if (ferror(ltfp)) mErrno(FAIL_IO, "le_update(): fread(lt)");
            pdebug("le_update(): eof on tran");
            break;
         }
         /* Sequence check on lt.addr */
         if (memcmp(lt.addr, lt_prev, TXADDRLEN) < 0) {
            mError(FAIL_IO, "le_update(): bad ltran.dat sort");
         }
         memcpy(lt_prev, lt.addr, TXADDRLEN);

         /* Check for multiple transactions on a single address:
         * '-' must come before 'A'
         * (Transaction file did not run out and its addr matches
         *  the previous transaction...)
         */
      } while (memcmp(lt.addr, taddr, TXADDRLEN) == 0);

      /* Only balances > Mfee are written to updated ledger. */
      if (cmp64(newle.balance, Mfee) > 0) {
         pdebug("le_update(): writing new balance");
         /* write new balance to temp file */
         if (fwrite(&newle, sizeof(LENTRY), 1, fp) != 1) {
            mError(FAIL_IO, "le_update(): bad write on temp ledger");
         }
         nout++;  /* count output records */
      } else pdebug("le_update(): new balance <= Mfee is not written");
   }  /* end while not both on EOF  -- updating ledger */

   fclose(fp);
   fclose(ltfp);
   fclose(lefp);
   if (nout) {
      /* if there are entries in ledger.tmp */
      remove("ledger.dat");
      rename("ledger.tmp", "ledger.dat");
      remove("ltran.dat.last");
      rename("ltran.dat", "ltran.dat.last");
   } else {
      remove("ledger.tmp");  /* remove empty temp file */
      mError(FAIL, "le_update(): the ledger is empty!");
   }

   pdebug("le_update(): wrote %u entries to new ledger", nout);
   pdebug("le_update(): completed in %gs", diffclocktime(clock(), ticks));

   /* success */
   return VEOK;

   /* failure / error handling */
FAIL_IO:
   fclose(fp);
   remove("ledger.tmp");
FAIL_OUT:
   fclose(ltfp);
FAIL_IN:
   fclose(lefp);
FAIL:

   return ecode;
}  /* end le_update() */

/* end include guard */
#endif
