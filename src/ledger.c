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
#include "tag.h"
#include "sort.h"
#include "global.h"
#include "error.h"

/* external support */
#include <string.h>
#include "sha256.h"
#include "extmath.h"
#include "extlib.h"
#include <errno.h>

static FILE *Lefp;
static long long Nledger;
static char Lefile[FILENAME_MAX];
word32 Sanctuary;
word32 Lastday;

/**
 * Hashed-based address comparison function. Includes tag in comparison.
 * @param a Pointer to address to compare
 * @param b Pointer to address to compare against
 * @return (int) value representing result
 * @retval 0 @a a is equal to @a b
 * @retval <0 @a a is less than @a b
 * @retval >0 @a a is greater than @a b
 */
int addr_compare(const void *a, const void *b)
{
   return memcmp(a, b, TXADDRLEN);
}

/**
 * Hashed-based address tag comparison function. ONLY compares tag.
 * @param a Pointer to data to compare
 * @param b Pointer to data to compare against
 * @return (int) value representing result
 * @retval 0 @a a is equal to @a b
 * @retval <0 @a a is less than @a b
 * @retval >0 @a a is greater than @a b
 */
int addr_tag_compare(const void *a, const void *b)
{
   return tag_compare(ADDR_TAG_PTR(a), ADDR_TAG_PTR(b));
}

/**
 * Equality check for address tags. ONLY compares tag.
 * Implements an efficient 12-byte check.
 * @param a Pointer to address with tag to compare
 * @param b Pointer to address with tag to compare against
 * @returns 1 if address tags match, else 0
 */
int addr_tag_equal(const void *a, const void *b)
{
   return tag_equal(ADDR_TAG_PTR(a), ADDR_TAG_PTR(b));
}  /* end tag_equal() */

/**
 * @private
 * Comparison function to sort LTRAN objects by address + transaction code.
 * DOES NOT CONSIDER Ledger transaction amount in sorting process.
 */
static int lt_compare(const void *va, const void *vb)
{
   return memcmp(va, vb, TXADDRLEN + 1);
}

/* Open ledger "ledger.dat" */
int le_open(const char *lefile)
{
   FILE *fp;
   long long offset;

   /* Already open? */
   if (Lefp) {
      if (strcmp(lefile, Lefile) == 0) return VEOK;
      /* ... no, opening different ledger */
   }

   /* open ledger and seek to EOF */
   fp = fopen(lefile, "rb");
   if (fp == NULL) return VERROR;
   if (fseek64(Lefp, 0LL, SEEK_END) != 0) {
      goto ERROR_CLEANUP;
   }

   /* determine file size (via position) and check validity */
   offset = ftell64(Lefp);
   if (offset == (-1)) goto ERROR_CLEANUP;
   if ((size_t) offset < sizeof(LENTRY) || offset % sizeof(LENTRY) != 0) {
      set_errno(EMCM_FILEDATA);
      goto ERROR_CLEANUP;
   }

   /* replace existing ledger */
   le_close();
   Lefp = fp;
   /* update static ledger unit values */
   Nledger = offset / sizeof(LENTRY);
   strncpy(Lefile, lefile, sizeof(Lefile) - 1);

   return VEOK;

   /* cleanup / error handling */
ERROR_CLEANUP:
   fclose(fp);

   return VERROR;
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
int le_find(const word8 *addr, LENTRY *le, word16 len)
{
   long long mid, hi, low;
   int cond;

   /* ledger must be open */
   if (Lefp == NULL) {
      set_errno(EMCM_LECLOSED);
      return 0;
   }

   /* check address pointer and non-zero search length */
   if (addr == NULL || le == NULL || len == 0) {
      set_errno(EINVAL);
      return 0;
   }

   /* clamp search length to TXADDRLEN */
   if (len > TXADDRLEN) len = TXADDRLEN;

   low = 0;
   hi = Nledger - 1;

   while(low <= hi) {
      mid = ((hi + low) / 2) * sizeof(LENTRY);
      if (fseek64(Lefp, mid, SEEK_SET) != 0) return 0;
      if (fread(le, sizeof(LENTRY), 1, Lefp) != 1) {
         if (!ferror(Lefp)) set_errno(EMCM_EOF);
         return 0;
      }
      cond = memcmp(addr, le->addr, len);
      if(cond == 0) return 1;  /* found target addr */
      if(cond < 0) hi = mid - 1; else low = mid + 1;
   }  /* end while */

   /* indicate successful operation in the absence of a result */
   set_errno(0);

   return 0;  /* not found */
}  /* end le_find() */

/**
 * Extract a ledger from a neo-genesis block. Checks sort.
 * @param ngfile Filename of the neo-genesis block
 * @param lefile Filename of the ledger
 * @return (int) value representing extraction result
 * @retval VERROR on error; check errno for details
 * @retval VEOK on success
*/
int le_extract(const char *ngfile, const char *lefile)
{
   LENTRY le;              /* buffer for Hashed ledger entries */
   NGHEADER ngh;           /* buffer for neo-genesis header */
   FILE *fp, *lfp;         /* FILE pointers */
   word8 paddr[TXADDRLEN]; /* ledger address sort check */
   word64 lbytes;
   size_t j, lcount;

   /* open files */
   fp = fopen(ngfile, "rb");
   if (fp == NULL) return VERROR;
   lfp = fopen(lefile, "wb");
   if (lfp == NULL) {
      fclose(fp);
      return VERROR;
   }

   /* read/check neo-genesis block header */
   if (fread(&ngh, sizeof(NGHEADER), 1, fp) != 1) goto RDERR_CLEANUP;
   if (get32(ngh.hdrlen) != sizeof(NGHEADER)) {
      set_errno(EMCM_HDRLEN);
      goto ERROR_CLEANUP;
   }
   put64(&lbytes, ngh.lbytes);
   if (lbytes < sizeof(LENTRY) || lbytes % sizeof(LENTRY) != 0) {
      set_errno(EMCM_FILEDATA);
      goto ERROR_CLEANUP;
   }

   /* process ledger data from fp, check sort, write to lfp */
   for (lcount = lbytes / sizeof(LENTRY), j = 0; j < lcount; j++) {
      if (fread(&le, sizeof(LENTRY), 1, fp) != 1) goto RDERR_CLEANUP;
      /* check ledger sort */
      if (j > 0 && addr_compare(le.addr, paddr) <= 0) {
         set_errno(EMCM_LESORT);
         goto ERROR_CLEANUP;
      }
      /* store entry for comparison */
      memcpy(paddr, le.addr, sizeof(le.addr));
      /* write hashed ledger entries to ledger file */
      if (fwrite(&le, sizeof(LENTRY), 1, lfp) != 1) goto ERROR_CLEANUP;
   }  /* end for() */

   /* close files */
   fclose(lfp);
   fclose(fp);

   /* ledger extracted */
   return VEOK;

   /* cleanup / error handling */
RDERR_CLEANUP:
   if (!ferror(fp)) {
      set_errno(EMCM_EOF);
   }
ERROR_CLEANUP:
   fclose(lfp);
   fclose(fp);

   return VERROR;
}  /* end le_extract() */

/**
 * Apply the Sanctuary Protocol to renew the ledger.
 * @return (int) value representing renew result
 * @retval VERROR on error; check errno for details
 * @retval VEOK on success
*/
int le_renew(void)
{
   FILE *fp;
   LENTRY le;
   word32 sanctuary[2];

   /* ledger must be open */
   if (Lefp == NULL) {
      set_errno(EMCM_LECLOSED);
      return VERROR;
   }

   if(Sanctuary == 0) return VEOK;  /* success */
   sanctuary[0] = Sanctuary;
   sanctuary[1] = 0;

   /* open ledger and replacement files */
   fp = fopen("ledger.renew", "wb");
   if (fp == NULL) goto ERROR_CLEANUP;

   /* renew the ledger per Carousal requirements */
   for(rewind(Lefp);;) {
      if (fread(&le, sizeof(LENTRY), 1, Lefp) != 1) {
         if (ferror(fp)) goto ERROR_CLEANUP;
         break;  /* EOF */
      }
      if(sub64(le.balance, sanctuary, le.balance)) continue;
      if(cmp64(le.balance, Mfee) <= 0) continue;
      if (fwrite(&le, sizeof(LENTRY), 1, fp) != 1) goto ERROR_CLEANUP;
   }

   /* cleanup */
   fclose(fp);

   /* close / replace ledger */
   le_close();
   remove(Lefile);
   if (rename("ledger.renew", Lefile) != 0) {
      return VERROR;
   }

   /* return result of reopen ledger */
   return le_open(Lefile);

   /* cleanup / error handling */
ERROR_CLEANUP:
   fclose(fp);
   remove("ledger.renew");

   return VERROR;
}  /* end le_renew() */

/**
 * Update the ledger by applying deltas from a ledger transaction file.
 * Ledger transaction file is sorted by addr+code, '-' comes before 'A'.
 * Ledger file is kept sorted on addr.
 * @param ltfname Filename of the Ledger transaction file containing deltas
 * @return (int) value representing the update result
 * @retval VEBAD2 on malicious; check errno for details
 * @retval VERROR on error; check errno for details
 * @retval VEOK on success
 */
int le_update(const char *ltfname)
{
   LENTRY le, le_hold;        /* for ledger entry and ledger hold data */
   LTRAN lt;                  /* for ledger transaction data */
   FILE *fp, *lefp, *ltfp;    /* output, ledger, and ltran file pointers */
   word8 le_prev[TXADDRLEN];     /* for ledger sequence check */
   word8 lt_prev[TXADDRLEN + 1]; /* for ltran sequence check */
   word8 hold, empty;
   int compare, ecode;

   /* ledger must be open */
   if (Lefp == NULL) {
      set_errno(EMCM_LECLOSED);
      return VERROR;
   }

   /* sort the ledger transaction file */
   ecode = filesort(ltfname, sizeof(LTRAN), LEBUFSZ, lt_compare);
   if (ecode != 0) return VERROR;

   /* init for error handling */
   fp = ltfp = NULL;
   lefp = Lefp;

   /* fseek and read initial ledger entry */
   if (fseek64(lefp, 0LL, SEEK_SET) != 0) return VERROR;
   if (fread(&le, sizeof(LENTRY), 1, lefp) != 1) {
      if (!ferror(lefp)) set_errno(EMCM_EOF);
      return VERROR;
   }
   /* open and read initial ledger transaction */
   ltfp = fopen(ltfname, "rb");
   if (ltfp == NULL) return VERROR;
   if (fread(&lt, sizeof(LTRAN), 1, ltfp) != 1) {
      if (!ferror(ltfp)) set_errno(EMCM_EOF);
      goto ERROR_CLEANUP;
   }

   /* generate temporary filename and open as new ledger */
   fp = fopen("ledger.update", "wb");
   if (fp == NULL) goto ERROR_CLEANUP;

   /* iterate through files while either files are NOT EOF */
   for (hold = 0, empty = 1; lefp != NULL || ltfp != NULL; ) {

      /* check ledger transaction file is open for processing */
      if (ltfp != NULL) {
         /* only perform initial comparison on ledger entry file */
         if (lefp != NULL) compare = addr_compare(le.addr, lt.addr);

         /* if ledger entry compares AFTER ledger transaction, OR
          * if ledger entry file is EOF... */
         if (compare > 0 || lefp == NULL) {
            /* assume malicious intent where non-CREDIT ('A') code here */
            if (lt.trancode[0] != 'A') {
               set_errno(EMCM_LTCREDIT);
               goto DROP_CLEANUP;
            }
            /* hold ledger entry while associated file is open */
            if (lefp != NULL) {
               memcpy(&le_hold, &le, sizeof(LENTRY));
               hold = 1;
            }
            /* clear ledger entry data, set ledger transaction address */
            memset(&le, 0, sizeof(LENTRY));
            memcpy(le.addr, lt.addr, TXADDRLEN);
            /* set compare for ledger transaction processing */
            compare = 0;
         }  /* end if (compare > 0 || lefp == NULL) */

         /* while ledger entry compares EQUAL TO ledger transaction... */
         while (compare == 0) {
            /* apply ledger transaction */
            switch (lt.trancode[0]) {
               case 'A':
                  /* transaction CREDIT operation */
                  if (add64(le.balance, lt.amount, le.balance)) {
                     /** @todo: reconsider math overflow as error? */
                     /* set_errno(EMCM_MATH64_OVERFLOW); */
                     /* goto FAIL_DROP; */
                     memset(le.balance, 0, sizeof(le.balance));
                  }
                  break;
               case '-':
                  /* transaction DEBIT operation */
                  /* ... assume malicious intent where balance != amount */
                  if (cmp64(le.balance, lt.amount) != 0) {
                     set_errno(EMCM_LTDEBIT);
                     goto DROP_CLEANUP;
                  }
                  memset(le.balance, 0, sizeof(le.balance));
                  break;
               default:
                  /* invalid transaction operation */
                  set_errno(EMCM_LTCODE);
                  goto ERROR_CLEANUP;
            }
            /* read next ledger transaction */
            memcpy(lt_prev, lt.addr, TXADDRLEN + 1);
            if (fread(&lt, sizeof(LTRAN), 1, ltfp) != 1) {
               if (ferror(ltfp)) goto ERROR_CLEANUP;
               /* EOF -- cleanup, break inner loop */
               fclose(ltfp);
               ltfp = NULL;
               break;
            }
            /* check sort -- MUST BE ascending, ALLOW duplicates */
            /* NOTE: sort check SHOULD include transaction code */
            if (memcmp(lt_prev, lt.addr, TXADDRLEN + 1) > 0) {
               set_errno(EMCM_LTSORT);
               goto ERROR_CLEANUP;
            }
            /* recompare latest ledger transaction */
            compare = addr_compare(le.addr, lt.addr);
         }  /* end while (compare == 0) */
      }  /* end if (ltfp != NULL) */

      /* if lendger entry compares BEFORE ledger transaction, OR
       * ledger transaction file is EOF... */
      if (compare < 0 || ltfp == NULL) {
         /* if ledger entry balance > MFEE, write to output */
         if (cmp64(le.balance, Mfee) > 0) {
            if (fwrite(&le, sizeof(LENTRY), 1, fp) != 1) goto ERROR_CLEANUP;
            /* flag output not empty */
            empty = 0;
         }
         /* if ledger entry file open... */
         if (lefp != NULL) {
            /* copy ledger hold to ledger entry, OR... */
            if (hold) {
               memcpy(&le, &le_hold, sizeof(LENTRY));
               hold = 0;
               continue;
            }
            /* read next ledger transaction, AND... */
            memcpy(le_prev, le.addr, TXADDRLEN);
            if (fread(&le, sizeof(LENTRY), 1, lefp) != 1) {
               if (ferror(lefp)) goto ERROR_CLEANUP;
               /* EOF -- DO NOT CLOSE, just decouple from Lefp */
               /* fclose(lefp); */
               lefp = NULL;
               continue;
            }
            /* check sort -- MUST BE ascending, NO duplicates */
            if (memcmp(le_prev, le.addr, TXADDRLEN) >= 0) {
               set_errno(EMCM_LESORT);
               goto ERROR_CLEANUP;
            }
         }  /* end  if (lefp != NULL) */
      }  /* end if (compare < 0... */
   }  /* end while () */
   /* empty ledger check */
   if (empty) {
      set_errno(EMCM_LEEMPTY);
      goto ERROR_CLEANUP;
   }

   /* cleanup -- ltfp already closed */
   fclose(fp);

   /* close / replace ledger */
   le_close();
   remove(Lefile);
   if (rename("ledger.update", Lefile) != 0) return VERROR;

   /* return result of reopen ledger */
   return le_open(Lefile);

   /* cleanup / error handling */
ERROR_CLEANUP:
   ecode = VERROR;
   goto CLEANUP;
DROP_CLEANUP:
   ecode = VEBAD2;
CLEANUP:
   if (ltfp) fclose(ltfp);
   if (fp) {
      fclose(fp);
      remove("ledger.update");
   }

   return ecode;
}  /* end le_update() */

/**
 * Tag comparison function.
 * @param a Pointer to tag to compare
 * @param b Pointer to tag to compare against
 * @returns (int) value representing result
 * @retval >0 if tag A is greater than tag B
 * @retval <0 if tag A is less than tag B
 * @retval 0 if tags are equal
 */
int tag_compare(const void *a, const void *b)
{
   return memcmp(a, b, TXTAGLEN);
}  /* end tag_cmp() */

/**
 * Equality check for tags. Implements an efficient 12-byte check.
 * @param a Pointer to tag to check
 * @param b Pointer to tag to check against
 * @returns 1 if tags match, else 0
 */
int tag_equal(const void *a, const void *b)
{
   return (
      ((word32 *) a)[0] == ((word32 *) b)[0] &&
      ((word32 *) a)[1] == ((word32 *) b)[1] &&
      ((word32 *) a)[2] == ((word32 *) b)[2]
   );
}  /* end tag_equal() */

/* end include guard */
#endif
