/**
 * @file tx.h
 * @brief Mochimo transaction support.
 * @copyright Adequate Systems LLC, 2018-2022. All Rights Reserved.
 * <br />For license information, please refer to ../LICENSE.md
*/

/* include guard */
#ifndef MOCHIMO_TX_H
#define MOCHIMO_TX_H


/* internal support */
#include "types.h"
#include "network.h"

/* extended-c support */
#include "extint.h"
#ifndef _WIN32
   #include <sys/file.h>   /* flock() */

#endif

/* C/C++ compatible function prototypes */
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN32
   int lock(char *lockfile, int seconds);
   int unlock(int fd);

#endif

int tx_bot_activate(const char *filename);
int tx_bot_is_active(void);
int tx_bot_process(void);

int tx_fread(TXENTRY *tx, FILE *stream);
int tx_fwrite(const TXENTRY *tx, FILE *stream);
void tx_hash(const TXENTRY *tx, int full, void *out);
int tx_read(TXENTRY *tx, const void *buf, size_t bufsz);
int tx_val(const TXENTRY *txe, const void *bnum, const void *mfee);
int txe_val(const TXENTRY *txe, const void *bnum, const void *mfee);
int txcheck(const word8 *src_addr);
int txclean(const char *txfname, const char *bcfname);
pid_t mgc(word32 ip);
pid_t mirror(void);
int process_tx(NODE *np);

#ifdef __cplusplus
}  /* end extern "C" */
#endif

/* end include guard */
#endif
