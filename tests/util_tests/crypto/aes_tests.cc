/*
  Copyright(c) 2011-2017 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <gtest/gtest.h>
#include <isa-l_crypto/aes_gcm.h>  //FROM ISA LIB
#include <sys/time.h>
#include <unistd.h>

// IV is the Initialization Vector. It is to be completely random. It is
// necessary for the GCM mode of AES to function.

// AAD is Authenticated Additional Data. That is, data that is not encrypted
// into the cipher text, but is authenticated. Useful for sending packets
// that have encrypted payloads but unencrypted headers.
// Not sure if necessary.

struct perf {
  struct timeval tv;
};

static inline int perf_start(struct perf *p) {
  return gettimeofday(&(p->tv), 0);
}
static inline int perf_stop(struct perf *p) {
  return gettimeofday(&(p->tv), 0);
}

static inline void perf_print(struct perf stop, struct perf start,
                              long long dsize) {
  long long secs = stop.tv.tv_sec - start.tv.tv_sec;
  long long usecs = secs * 1000000 + stop.tv.tv_usec - start.tv.tv_usec;

  printf("runtime = %lld usecs", usecs);
  if (dsize != 0) {
    printf(", bandwidth %lld MB in %.4f sec = %.2f MB/s\n",
           dsize / (1024 * 1024), usecs / 1000000.0, dsize / usecs * 1.0);
  } else
    printf("\n");
}

// Fill a vector with random values
void mk_rand_data(uint8_t *data, uint32_t size) {
  for (size_t i = 0; i < size; i++) {
    *data++ = rand();
  }
}

// Compare two vectors to see if they have the same values
int check_data(const uint8_t *test, const uint8_t *expected, uint64_t len,
               int vect, const char *data_name) {
  int OK = 1;

  int mismatch = memcmp(test, expected, len);
  if (mismatch) {
    OK = 0;
    printf("  v[%d] expected results don't match %s", vect, data_name);

    for (size_t a = 0; a < len; a++) {
      if (test[a] != expected[a]) {
        printf(" '%x' != '%x' at %lx of %lx\n", test[a], expected[a], a, len);
        break;
      }
    }
  }
  return OK;
}

// Correctness test for encrypting 16 byte payloads
static constexpr size_t kTestLenSmall = 16;
static constexpr size_t kAADLength = 16;
TEST(AesGcmTest, CorrectnessSmall) {
  struct gcm_data gdata_small;  // defined in aes_gcm
  // Table valus for small test, including Key, Plaintext, Cyphertext, Tag
  // Vector 3 in gcm_vectors.h
  // Key is 16 bytes
  uint8_t key_small[GCM_128_KEY_LEN] = {0xc9, 0x39, 0xcc, 0x13, 0x39, 0x7c,
                                        0x1d, 0x37, 0xde, 0x6a, 0xe0, 0xe1,
                                        0xcb, 0x7c, 0x42, 0x3c};
  // Message is 16 bytes
  unsigned char plaintext_small_true[kTestLenSmall] = {
      0xc3, 0xb3, 0xc4, 0x1f, 0x11, 0x3a, 0x31, 0xb7,
      0x3d, 0x9a, 0x5c, 0xd4, 0x32, 0x10, 0x30, 0x69};
  unsigned char plaintext_small_test[kTestLenSmall];
  // Cypher text will be same size as message
  unsigned char cyphertext_small_true[kTestLenSmall] = {
      0x93, 0xfe, 0x7d, 0x9e, 0x9b, 0xfd, 0x10, 0x34,
      0x8a, 0x56, 0x06, 0xe5, 0xca, 0xfa, 0x73, 0x54};
  unsigned char cyphertext_small_test[kTestLenSmall];
  // Authentication tag will be 16 bytes
  unsigned char auth_tag_small_true[MAX_TAG_LEN] = {
      0x00, 0x32, 0xa1, 0xdc, 0x85, 0xf1, 0xc9, 0x78,
      0x69, 0x25, 0xa2, 0xe7, 0x1d, 0x82, 0x72, 0xdd};
  unsigned char auth_tag_small_test[MAX_TAG_LEN];
  // Initialization vector will be 16 bytes
  unsigned char IV_small[GCM_IV_LEN] = {0xb3, 0xd8, 0xcc, 0x01, 0x7c, 0xbb,
                                        0x89, 0xb3, 0x9e, 0x0f, 0x67, 0xe2,
                                        0x0,  0x0,  0x0,  0x1};
  // Additional data will be 16 bytes
  unsigned char AAD_small[kAADLength] = {0x24, 0x82, 0x56, 0x02, 0xbd, 0x12,
                                         0xa9, 0x84, 0xe0, 0x09, 0x2d, 0x3e,
                                         0x44, 0x8e, 0xda, 0x5f};
  printf(
      "AES GCM correctness parameters small plain text length:%zu; "
      "IV length:%d; ADD length:%zu; Key length:%d \n",
      kTestLenSmall, GCM_IV_LEN, kAADLength, GCM_128_KEY_LEN);

  // Prefills the gcm data with key values for each round
  // and the initial sub hash key for tag encoding
  // This is only required once for a given key
  aesni_gcm128_pre(key_small, &gdata_small);
  // Correctness for small Plaintext
  {
    aesni_gcm128_enc(&gdata_small, cyphertext_small_test, plaintext_small_true,
                     kTestLenSmall, IV_small, AAD_small, kAADLength,
                     auth_tag_small_test, MAX_TAG_LEN);
    check_data(cyphertext_small_test, cyphertext_small_true, kTestLenSmall, 0,
               "ISA-L Encrypt small plaintext check of Cyphertext (C)");
    check_data(auth_tag_small_test, auth_tag_small_true, MAX_TAG_LEN, 0,
               "ISA-L Encrypt small plaintext check of Authentication Tag (T)");
  }

  {
    aesni_gcm128_dec(&gdata_small, plaintext_small_test, cyphertext_small_true,
                     kTestLenSmall, IV_small, AAD_small, kAADLength,
                     auth_tag_small_test, MAX_TAG_LEN);
    check_data(plaintext_small_test, plaintext_small_true, kTestLenSmall, 0,
               "ISA-L Decrypt small plaintext check of Plaintext (P)");
    check_data(auth_tag_small_test, auth_tag_small_true, MAX_TAG_LEN, 0,
               "ISA-L Decrypt small plaintext check of Authentication Tag (T)");
  }
}

// Correctness test for encrypting 64 byte payloads
static constexpr size_t kTestLenBig2 = 60;
static constexpr size_t kAADLengthBig = 20;
TEST(AesGcmTest, CorrectnessBig) {
  struct gcm_data gdata_big;  // defined in aes_gcm
  // Table values for large test, including Key, Plaintext, Cyphertext, Tag
  // Vector 8 in gcm_vectors.h
  // Key is 128 bits
  uint8_t key_big[GCM_128_KEY_LEN] = {0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65,
                                      0x73, 0x1c, 0x6d, 0x6a, 0x8f, 0x94,
                                      0x67, 0x30, 0x83, 0x08};
  // Message is 60 bytes
  unsigned char plaintext_big_true[kTestLenBig2] = {
      0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5,
      0xaf, 0xf5, 0x26, 0x9a, 0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
      0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72, 0x1c, 0x3c, 0x0c, 0x95,
      0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
      0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39};
  unsigned char plaintext_big_test[kTestLenBig2];
  // Cypher will be 60 bytes long too
  unsigned char cyphertext_big_true[kTestLenBig2] = {
      0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7,
      0x84, 0xd0, 0xd4, 0x9c, 0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
      0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e, 0x21, 0xd5, 0x14, 0xb2,
      0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
      0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91};
  unsigned char cyphertext_big_test[kTestLenBig2];
  // Tag will be 16 bytes
  unsigned char auth_tag_big_true[MAX_TAG_LEN] = {
      0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb,
      0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a, 0x47};
  unsigned char auth_tag_big_test[MAX_TAG_LEN];
  // IV will be 16 bytes
  unsigned char IV_big[GCM_IV_LEN] = {0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
                                      0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88,
                                      0x0,  0x0,  0x0,  0x1};
  // Additional data will be 20 bytes
  unsigned char AAD_big[(kAADLengthBig)] = {
      0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed,
      0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef, 0xab, 0xad, 0xda, 0xd2};

  // Prefills the gcm data with key values for each round
  // and the initial sub hash key for tag encoding
  // This is only required once for a given key
  aesni_gcm128_pre(key_big, &gdata_big);
  printf(
      "AES GCM correctness parameters big plain text length:%zu; "
      "IV length:%d; ADD length:%zu; Key length:%d \n",
      kTestLenBig2, GCM_IV_LEN, kAADLengthBig, GCM_128_KEY_LEN);
  // Correctness for big Plaintext
  {
    aesni_gcm128_enc(&gdata_big, cyphertext_big_test, plaintext_big_true,
                     kTestLenBig2, IV_big, AAD_big, kAADLengthBig,
                     auth_tag_big_test, MAX_TAG_LEN);
    check_data(cyphertext_big_test, cyphertext_big_true, kTestLenBig2, 0,
               "ISA-L Encrypt big plaintext check of Cyphertext (C)");
    check_data(auth_tag_big_test, auth_tag_big_true, MAX_TAG_LEN, 0,
               "ISA-L Encrypt big plaintext check of Authentication Tag (T)");
  }

  {
    aesni_gcm128_dec(&gdata_big, plaintext_big_test, cyphertext_big_true,
                     kTestLenBig2, IV_big, AAD_big, kAADLengthBig,
                     auth_tag_big_test, MAX_TAG_LEN);
    check_data(plaintext_big_test, plaintext_big_true, kTestLenBig2, 0,
               "ISA-L Decrypt big plaintext check of Plaintext (P)");
    check_data(auth_tag_big_test, auth_tag_big_true, MAX_TAG_LEN, 0,
               "ISA-L Decrypt big plaintext check of Authentication Tag (T)");
  }
}

// Perf test for encrypting 16 byte payloads
// Interested in: millions of n-byte payloads encrypted per second
// Note: Don't pay overhead of key setup every time
static constexpr size_t kTestLoops = 500000;  // Larger?
TEST(AesGcmTest, PerfSmall) {
  struct gcm_data gdata;  // defined in aes_gcm

  uint8_t key128[GCM_128_KEY_LEN];
  unsigned char *plaintext_small, *cyphertext_small, *auth_tag_small, *IV, *AAD;

  plaintext_small = reinterpret_cast<unsigned char *>(malloc(kTestLenSmall));
  cyphertext_small = reinterpret_cast<unsigned char *>(malloc(kTestLenSmall));
  auth_tag_small = reinterpret_cast<unsigned char *>(malloc(MAX_TAG_LEN));
  IV = reinterpret_cast<unsigned char *>(malloc(GCM_IV_LEN));
  AAD = reinterpret_cast<unsigned char *>(malloc(kAADLength));

  mk_rand_data(key128, sizeof(key128));
  mk_rand_data(plaintext_small, kTestLenSmall);
  mk_rand_data(AAD, kAADLength);
  uint8_t const IVend[] = GCM_IV_END_MARK;
  mk_rand_data(IV, GCM_IV_LEN);
  memcpy(&IV[GCM_IV_END_START], IVend, sizeof(IVend));

  // Pre-fills the gcm data with key values for each round
  // and the initial sub hash key for tag encoding
  // This is only required once for a given key
  aesni_gcm128_pre(key128, &gdata);

  printf(
      "AES GCM performace parameters small plain text length:%zu; "
      "IV length:%d; ADD length:%zu; Key length:%d \n",
      kTestLenSmall, GCM_IV_LEN, kAADLength, GCM_128_KEY_LEN);

  // Performance for small plaintext
  {
    struct perf start, stop;
    perf_start(&start);
    for (size_t i = 0; i < kTestLoops; i++) {
      aesni_gcm128_enc(&gdata, cyphertext_small, plaintext_small, kTestLenSmall,
                       IV, AAD, kAADLength, auth_tag_small, MAX_TAG_LEN);
    }
    perf_stop(&stop);
    printf("aes_gcm_enc :");
    perf_print(stop, start, static_cast<long long>(kTestLenSmall) * kTestLoops);
  }

  {
    struct perf start, stop;
    perf_start(&start);
    for (size_t i = 0; i < kTestLoops; i++) {
      aesni_gcm128_dec(&gdata, plaintext_small, cyphertext_small, kTestLenSmall,
                       IV, AAD, kAADLength, auth_tag_small, MAX_TAG_LEN);
    }
    perf_stop(&stop);
    printf("aes_gcm_dec :");
    perf_print(stop, start, static_cast<long long>(kTestLenSmall * kTestLoops));
  }
}

// Perf test for encrypting 100 byte payloads
// Interested in: millions of n_byte payloads encrypted per second
// Note: Don't pay overhead of key setup every time
static constexpr size_t kTestLenBig = 100;
TEST(AesGcmTest, PerfBig) {
  struct gcm_data gdata;  // defined in aes_gcm

  uint8_t key128[GCM_128_KEY_LEN];
  unsigned char *plaintext_big, *cyphertext_big, *auth_tag_big, *IV, *AAD;

  plaintext_big = reinterpret_cast<unsigned char *>(malloc(kTestLenBig));
  cyphertext_big = reinterpret_cast<unsigned char *>(malloc(kTestLenBig));
  auth_tag_big = reinterpret_cast<unsigned char *>(malloc(MAX_TAG_LEN));

  IV = reinterpret_cast<unsigned char *>(malloc(GCM_IV_LEN));
  AAD = reinterpret_cast<unsigned char *>(malloc(kAADLength));

  mk_rand_data(plaintext_big, kTestLenBig);
  mk_rand_data(AAD, kAADLength);
  mk_rand_data(IV, GCM_IV_LEN);
  mk_rand_data(key128, sizeof(key128));

  // Prefills the gcm data with key values for each round
  // and the initial sub hash key for tag encoding
  // This is only required once for a given key
  aesni_gcm128_pre(key128, &gdata);

  printf(
      "AES GCM performance parameters big plain text length:%zu; "
      "IV length:%d; ADD length:%zu; Key length:%d \n",
      kTestLenBig, GCM_IV_LEN, kAADLength, GCM_128_KEY_LEN);

  // Performance for big plaintext
  {
    struct perf start, stop;
    perf_start(&start);
    for (size_t i = 0; i < kTestLoops; i++) {
      aesni_gcm128_enc(&gdata, cyphertext_big, plaintext_big, kTestLenBig, IV,
                       AAD, kAADLength, auth_tag_big, MAX_TAG_LEN);
    }
    perf_stop(&stop);
    printf("aes_gcm_enc : ");
    perf_print(stop, start, static_cast<long long>(kTestLenBig) * kTestLoops);
  }

  {
    struct perf start, stop;
    perf_start(&start);
    for (size_t i = 0; i < kTestLoops; i++) {
      aesni_gcm128_dec(&gdata, plaintext_big, cyphertext_big, kTestLenBig, IV,
                       AAD, kAADLength, auth_tag_big, MAX_TAG_LEN);
    }
    perf_stop(&stop);
    printf("aes_gcm_dec : ");
    perf_print(stop, start, static_cast<long long>(kTestLenBig * kTestLoops));
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
