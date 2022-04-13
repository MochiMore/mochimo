
/* must be declared before includes */
#ifndef CUDA
   #define CUDA
#endif

#include <string.h>
#include "_assert.h"
#include "extint.h"
#include "peach.h"

#define NUMVECTORS  5

/* Peach test vectors taken directly from the Mochimo Blockchain Tfile */
static word8 Pvector[NUMVECTORS][BTSIZE] = {
   {  /* Block 0x12852 (75858) - first Peach block, inevitably pseudo */
      0xca, 0x30, 0x56, 0x33, 0x1e, 0x3c, 0x48, 0x4d, 0xa7, 0xdd,
      0xa2, 0xdd, 0x36, 0x28, 0xaa, 0x12, 0x5d, 0x5d, 0xbb, 0xf5,
      0x1e, 0x02, 0x96, 0x94, 0x30, 0xdc, 0xcf, 0x59, 0x12, 0x8e,
      0x9c, 0x0c, 0x52, 0x28, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0xaa, 0xda, 0x1b, 0x5d, 0x2e, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x5f, 0xde, 0x1b, 0x5d, 0xf3, 0x4d,
      0x15, 0xe8, 0x86, 0x0d, 0xc5, 0x53, 0x7d, 0x40, 0xe6, 0x7c,
      0x93, 0x4d, 0x62, 0xb2, 0x66, 0x29, 0xc0, 0x9b, 0x0f, 0xb3,
      0xa8, 0x67, 0x23, 0x8d, 0xc5, 0x95, 0x48, 0x04, 0x65, 0x40
   },
   {  /* Block 0x1285f (75871) - low diff pseudo */
      0xb0, 0xdc, 0x58, 0xa1, 0x2e, 0x99, 0xdd, 0xd1, 0x01, 0xa9,
      0x5e, 0x4f, 0xf8, 0x20, 0xaf, 0x60, 0x6d, 0x0b, 0xe3, 0x99,
      0x1d, 0xe2, 0xb0, 0x15, 0xd8, 0xd7, 0x0b, 0xd2, 0xd6, 0x53,
      0x6a, 0x81, 0x5f, 0x28, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0xdb, 0x0a, 0x1c, 0x5d, 0x21, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x90, 0x0e, 0x1c, 0x5d, 0x41, 0x85,
      0xf2, 0x88, 0x09, 0x44, 0x32, 0x7f, 0xfb, 0x76, 0x1c, 0x32,
      0xc3, 0x12, 0x8e, 0xf1, 0xbf, 0xe2, 0xc0, 0x97, 0xfd, 0xc9,
      0xd3, 0x87, 0xc3, 0xf7, 0x0b, 0xe6, 0xe5, 0x66, 0x5e, 0xae
   },
   {  /* Block 0x128ff (76031) */
      0xc8, 0x7f, 0xdc, 0x08, 0xad, 0x6a, 0x53, 0xef, 0x5f, 0xd0,
      0xf9, 0x8b, 0xf2, 0xa6, 0x6d, 0xb6, 0xc5, 0x84, 0x26, 0x78,
      0x7c, 0xb7, 0x71, 0x24, 0x4e, 0xf7, 0xfc, 0x57, 0x4b, 0x45,
      0x46, 0x95, 0xff, 0x28, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xf4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
      0x00, 0x00, 0xbc, 0xc2, 0x1c, 0x5d, 0x20, 0x00, 0x00, 0x00,
      0xf0, 0xf9, 0x58, 0xeb, 0x58, 0xeb, 0xe5, 0x0b, 0x2c, 0xc8,
      0xbc, 0x84, 0xf4, 0xf0, 0x0b, 0x74, 0x80, 0xe9, 0xd2, 0xf6,
      0x10, 0xfe, 0x61, 0x12, 0x74, 0x38, 0xc8, 0xf7, 0xe8, 0x93,
      0x0a, 0x6f, 0x0f, 0x77, 0xe2, 0x01, 0xa5, 0x01, 0x12, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xd6,
      0x01, 0x6b, 0xf2, 0x01, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x2b, 0xc4, 0x1c, 0x5d, 0xe4, 0x8a,
      0xca, 0x7b, 0xfa, 0xbd, 0xdb, 0x92, 0xaf, 0xbe, 0x08, 0x52,
      0x7b, 0xfd, 0x49, 0x71, 0x0d, 0xfc, 0x5f, 0xff, 0xe8, 0xed,
      0x15, 0xdf, 0x5b, 0x7c, 0x7a, 0x30, 0xe4, 0xb4, 0x0a, 0x51
   },
   {  /* Block 0x12fff (77823) */
      0xfa, 0x3c, 0x9f, 0x10, 0x8d, 0x12, 0x81, 0x56, 0xcc, 0x68,
      0x31, 0x82, 0x55, 0xc5, 0x14, 0xe7, 0x19, 0x9b, 0xdc, 0x6c,
      0x70, 0xe8, 0xdc, 0xf5, 0xb8, 0xa5, 0x12, 0x77, 0x34, 0xdf,
      0x60, 0x5e, 0xff, 0x2f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xf4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
      0x00, 0x00, 0xb0, 0xeb, 0x24, 0x5d, 0x24, 0x00, 0x00, 0x00,
      0xd5, 0xcf, 0x68, 0x1d, 0x3f, 0x00, 0x6f, 0x3f, 0x0c, 0x49,
      0xee, 0x6f, 0x2c, 0xd9, 0x03, 0x08, 0xf5, 0x77, 0xd3, 0x90,
      0x63, 0x27, 0x44, 0xea, 0x31, 0x4b, 0x36, 0x88, 0x17, 0xd0,
      0x35, 0x94, 0xfd, 0xd9, 0x01, 0x68, 0xd8, 0x01, 0x18, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x0f,
      0x54, 0xd8, 0x01, 0xda, 0x15, 0x03, 0x01, 0x05, 0x3d, 0x81,
      0x00, 0x00, 0x00, 0x00, 0x7d, 0xed, 0x24, 0x5d, 0x69, 0xc5,
      0xde, 0xe1, 0x63, 0xbd, 0x2d, 0x72, 0xc2, 0x5b, 0xdc, 0xf7,
      0x3f, 0xc3, 0x61, 0x5a, 0x85, 0x34, 0x17, 0xef, 0x53, 0xc5,
      0x3f, 0x4f, 0x3b, 0xe5, 0x9a, 0x1d, 0x68, 0x88, 0x8c, 0xae
   },
   {  /* Block 0x1ffff (131071) */
      0x54, 0xb4, 0x14, 0x30, 0xe1, 0xaf, 0x0a, 0xe1, 0xfb, 0x4b,
      0x2a, 0xbf, 0x4b, 0x92, 0x33, 0x4e, 0x88, 0x66, 0x7c, 0xae,
      0xdc, 0x23, 0xdc, 0x45, 0x72, 0x3b, 0xb4, 0xdc, 0xbe, 0x37,
      0x2e, 0xa9, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xf4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00,
      0x00, 0x00, 0x6b, 0x8f, 0x15, 0x5e, 0x22, 0x00, 0x00, 0x00,
      0xed, 0x5a, 0xc4, 0xb3, 0xd4, 0xe1, 0x12, 0xb3, 0x2e, 0xe7,
      0xa1, 0xd7, 0xcc, 0xde, 0x55, 0xeb, 0xb5, 0x05, 0x6a, 0x08,
      0x1f, 0x0d, 0x0d, 0x12, 0xfd, 0x80, 0x7f, 0xa7, 0x9a, 0x60,
      0x5d, 0x9c, 0x0c, 0xff, 0x01, 0x05, 0xc0, 0x01, 0x1e, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb, 0xe1,
      0x01, 0x0f, 0x05, 0x60, 0xc7, 0x03, 0x01, 0x57, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xf1, 0x8f, 0x15, 0x5e, 0x57, 0xa5,
      0xba, 0x1b, 0xb3, 0xed, 0x92, 0x6f, 0x99, 0xf4, 0xeb, 0xe1,
      0xe2, 0xf1, 0x8f, 0xa0, 0x85, 0xe9, 0x58, 0x00, 0x9c, 0x1b,
      0xab, 0x2e, 0x59, 0xf6, 0x12, 0xb5, 0x8c, 0x04, 0xae, 0x66
   }
};

/* Known hexadecimal results to Pvectors for peach_checkhash() */
static word8 Pexpect[NUMVECTORS][SHA256LEN] = {
   { 0 }, { 0 },  /* first 2 test vectors are pseudoblocks */  {
      0x00, 0x00, 0x00, 0x00, 0xf5, 0xc5, 0xa5, 0xce, 0xf0, 0xd9, 0x7e,
      0x71, 0x6b, 0xea, 0xe6, 0xe1, 0x37, 0x9d, 0x7d, 0x06, 0xb4, 0xe5,
      0xd9, 0x08, 0xe9, 0x8b, 0x0e, 0x4b, 0x8e, 0xca, 0xe2, 0xfc
   }, {
      0x00, 0x00, 0x00, 0x00, 0x01, 0xde, 0x9c, 0x4c, 0xd9, 0x6d, 0x9d,
      0xfe, 0xee, 0xc3, 0xe1, 0xc7, 0x58, 0x04, 0xfa, 0x24, 0xb6, 0x7e,
      0x80, 0x88, 0xe0, 0x1d, 0xe6, 0xf7, 0x18, 0xf2, 0x30, 0x1f
   }, {
      0x00, 0x00, 0x00, 0x00, 0x26, 0xd2, 0xfc, 0xb9, 0x4c, 0x59, 0x7f,
      0xd2, 0x32, 0x69, 0x98, 0x9c, 0xb1, 0x79, 0x83, 0x42, 0xe4, 0x6a,
      0xe8, 0x5d, 0x04, 0x14, 0xc4, 0x77, 0xa2, 0x24, 0x3a, 0x22
   }
};

int main()
{  /* check peach_checkhash() final hash results match expected */
   BTRAILER bt;
   word8 digest[SHA256LEN];
   int j;

   for (j = 2; j < NUMVECTORS; j++) {
      memset(digest, 0 , SHA256LEN);
      memcpy(&bt, Pvector[j], BTSIZE);
      ASSERT_EQ(peach_checkhash_cuda(&bt, digest), 0);
      ASSERT_CMP(digest, Pexpect[j], SHA256LEN);
   }
}
