/**
 * @file parallel.h
 * @brief Mochimo (optional) parallel support.
 * @copyright Adequate Systems LLC, 2024. All Rights Reserved.
 * <br />For license information, please refer to ../LICENSE.md
*/

/* include guard */
#ifndef MOCHIMO_PARALLEL_H
#define MOCHIMO_PARALLEL_H


/* use compiler option "-fopenmp" to enable OpenMP support...
 * NOTE: not all compilers come with built-in support for OpenMP.
 * Namely, clang on OSx (aliased by `gcc`, seriously wth), can support
 * OpenMP but is not built with OpenMP support by default. On such
 * systems, one should install GCC and set CC to the appropriate binary.
 * For example, `export CC=gcc-14` or `make <recipe> CC=gcc-14.
 */

#ifdef _OPENMP
   /* OpenMP supported */
   #include <omp.h>

   #ifndef DO_PRAGMA
   #define DO_PRAGMA(X) _Pragma(#X)
   #endif

   #define OMP_PARALLEL_(X) DO_PRAGMA(omp parallel X)
   #define OMP_CRITICAL_(X) DO_PRAGMA(omp critical X)
   #define OMP_ATOMIC_(X)   DO_PRAGMA(omp atomic X)
   #define OMP_SINGLE_(X)   DO_PRAGMA(omp single X)
   #define OMP_FOR_(X)      DO_PRAGMA(omp for X)

#else
   /* OpenMP not supported */
   #define OMP_PARALLEL_(X)
   #define OMP_CRITICAL_(X)
   #define OMP_ATOMIC_(X)
   #define OMP_SINGLE_(X)
   #define OMP_FOR_(X)

#endif

/* end include guard */
#endif