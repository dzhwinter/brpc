// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>   // for min()

#include "butil/atomicops.h"
#include <gtest/gtest.h>

// Number of bits in a size_t.
static const int kSizeBits = 8 * sizeof(size_t);
// The maximum size of a size_t.
static const size_t kMaxSize = ~static_cast<size_t>(0);
// Maximum positive size of a size_t if it were signed.
static const size_t kMaxSignedSize = ((size_t(1) << (kSizeBits-1)) - 1);
// An allocation size which is not too big to be reasonable.
static const size_t kNotTooBig = 100000;
// An allocation size which is just too big.
static const size_t kTooBig = ~static_cast<size_t>(0);

namespace {

using std::min;

// Fill a buffer of the specified size with a predetermined pattern
static void Fill(unsigned char* buffer, int n) {
  for (int i = 0; i < n; i++) {
    buffer[i] = (i & 0xff);
  }
}

// Check that the specified buffer has the predetermined pattern
// generated by Fill()
static bool Valid(unsigned char* buffer, int n) {
  for (int i = 0; i < n; i++) {
    if (buffer[i] != (i & 0xff)) {
      return false;
    }
  }
  return true;
}

// Check that a buffer is completely zeroed.
static bool ALLOW_UNUSED IsZeroed(unsigned char* buffer, int n) {
  for (int i = 0; i < n; i++) {
    if (buffer[i] != 0) {
      return false;
    }
  }
  return true;
}

// Check alignment
static void CheckAlignment(void* p, int align) {
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(p) & (align-1));
}

// Return the next interesting size/delta to check.  Returns -1 if no more.
static int NextSize(int size) {
  if (size < 100)
    return size+1;

  if (size < 100000) {
    // Find next power of two
    int power = 1;
    while (power < size)
      power <<= 1;

    // Yield (power-1, power, power+1)
    if (size < power-1)
      return power-1;

    if (size == power-1)
      return power;

    assert(size == power);
    return power+1;
  } else {
    return -1;
  }
}

template <class AtomicType>
static void TestAtomicIncrement() {
  // For now, we just test single threaded execution

  // use a guard value to make sure the NoBarrier_AtomicIncrement doesn't go
  // outside the expected address bounds.  This is in particular to
  // test that some future change to the asm code doesn't cause the
  // 32-bit NoBarrier_AtomicIncrement to do the wrong thing on 64-bit machines.
  struct {
    AtomicType prev_word;
    AtomicType count;
    AtomicType next_word;
  } s;

  AtomicType prev_word_value, next_word_value;
  memset(&prev_word_value, 0xFF, sizeof(AtomicType));
  memset(&next_word_value, 0xEE, sizeof(AtomicType));

  s.prev_word = prev_word_value;
  s.count = 0;
  s.next_word = next_word_value;

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 1), 1);
  EXPECT_EQ(s.count, 1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 2), 3);
  EXPECT_EQ(s.count, 3);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 3), 6);
  EXPECT_EQ(s.count, 6);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -3), 3);
  EXPECT_EQ(s.count, 3);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -2), 1);
  EXPECT_EQ(s.count, 1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -1), 0);
  EXPECT_EQ(s.count, 0);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -1), -1);
  EXPECT_EQ(s.count, -1);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, -4), -5);
  EXPECT_EQ(s.count, -5);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);

  EXPECT_EQ(butil::subtle::NoBarrier_AtomicIncrement(&s.count, 5), 0);
  EXPECT_EQ(s.count, 0);
  EXPECT_EQ(s.prev_word, prev_word_value);
  EXPECT_EQ(s.next_word, next_word_value);
}


#define NUM_BITS(T) (sizeof(T) * 8)


template <class AtomicType>
static void TestCompareAndSwap() {
  AtomicType value = 0;
  AtomicType prev = butil::subtle::NoBarrier_CompareAndSwap(&value, 0, 1);
  EXPECT_EQ(1, value);
  EXPECT_EQ(0, prev);

  // Use test value that has non-zero bits in both halves, more for testing
  // 64-bit implementation on 32-bit platforms.
  const AtomicType k_test_val = (static_cast<uint64_t>(1) <<
                                 (NUM_BITS(AtomicType) - 2)) + 11;
  value = k_test_val;
  prev = butil::subtle::NoBarrier_CompareAndSwap(&value, 0, 5);
  EXPECT_EQ(k_test_val, value);
  EXPECT_EQ(k_test_val, prev);

  value = k_test_val;
  prev = butil::subtle::NoBarrier_CompareAndSwap(&value, k_test_val, 5);
  EXPECT_EQ(5, value);
  EXPECT_EQ(k_test_val, prev);
}


template <class AtomicType>
static void TestAtomicExchange() {
  AtomicType value = 0;
  AtomicType new_value = butil::subtle::NoBarrier_AtomicExchange(&value, 1);
  EXPECT_EQ(1, value);
  EXPECT_EQ(0, new_value);

  // Use test value that has non-zero bits in both halves, more for testing
  // 64-bit implementation on 32-bit platforms.
  const AtomicType k_test_val = (static_cast<uint64_t>(1) <<
                                 (NUM_BITS(AtomicType) - 2)) + 11;
  value = k_test_val;
  new_value = butil::subtle::NoBarrier_AtomicExchange(&value, k_test_val);
  EXPECT_EQ(k_test_val, value);
  EXPECT_EQ(k_test_val, new_value);

  value = k_test_val;
  new_value = butil::subtle::NoBarrier_AtomicExchange(&value, 5);
  EXPECT_EQ(5, value);
  EXPECT_EQ(k_test_val, new_value);
}


template <class AtomicType>
static void TestAtomicIncrementBounds() {
  // Test increment at the half-width boundary of the atomic type.
  // It is primarily for testing at the 32-bit boundary for 64-bit atomic type.
  AtomicType test_val = static_cast<uint64_t>(1) << (NUM_BITS(AtomicType) / 2);
  AtomicType value = test_val - 1;
  AtomicType new_value = butil::subtle::NoBarrier_AtomicIncrement(&value, 1);
  EXPECT_EQ(test_val, value);
  EXPECT_EQ(value, new_value);

  butil::subtle::NoBarrier_AtomicIncrement(&value, -1);
  EXPECT_EQ(test_val - 1, value);
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestStore() {
  const AtomicType kVal1 = static_cast<AtomicType>(0xa5a5a5a5a5a5a5a5LL);
  const AtomicType kVal2 = static_cast<AtomicType>(-1);

  AtomicType value;

  butil::subtle::NoBarrier_Store(&value, kVal1);
  EXPECT_EQ(kVal1, value);
  butil::subtle::NoBarrier_Store(&value, kVal2);
  EXPECT_EQ(kVal2, value);

  butil::subtle::Acquire_Store(&value, kVal1);
  EXPECT_EQ(kVal1, value);
  butil::subtle::Acquire_Store(&value, kVal2);
  EXPECT_EQ(kVal2, value);

  butil::subtle::Release_Store(&value, kVal1);
  EXPECT_EQ(kVal1, value);
  butil::subtle::Release_Store(&value, kVal2);
  EXPECT_EQ(kVal2, value);
}

// This is a simple sanity check that values are correct. Not testing
// atomicity
template <class AtomicType>
static void TestLoad() {
  const AtomicType kVal1 = static_cast<AtomicType>(0xa5a5a5a5a5a5a5a5LL);
  const AtomicType kVal2 = static_cast<AtomicType>(-1);

  AtomicType value;

  value = kVal1;
  EXPECT_EQ(kVal1, butil::subtle::NoBarrier_Load(&value));
  value = kVal2;
  EXPECT_EQ(kVal2, butil::subtle::NoBarrier_Load(&value));

  value = kVal1;
  EXPECT_EQ(kVal1, butil::subtle::Acquire_Load(&value));
  value = kVal2;
  EXPECT_EQ(kVal2, butil::subtle::Acquire_Load(&value));

  value = kVal1;
  EXPECT_EQ(kVal1, butil::subtle::Release_Load(&value));
  value = kVal2;
  EXPECT_EQ(kVal2, butil::subtle::Release_Load(&value));
}

template <class AtomicType>
static void TestAtomicOps() {
  TestCompareAndSwap<AtomicType>();
  TestAtomicExchange<AtomicType>();
  TestAtomicIncrementBounds<AtomicType>();
  TestStore<AtomicType>();
  TestLoad<AtomicType>();
}

static void TestCalloc(size_t n, size_t s, bool ok) {
  char* p = reinterpret_cast<char*>(calloc(n, s));
  if (!ok) {
    EXPECT_EQ(NULL, p) << "calloc(n, s) should not succeed";
  } else {
    EXPECT_NE(reinterpret_cast<void*>(NULL), p) <<
        "calloc(n, s) should succeed";
    for (size_t i = 0; i < n*s; i++) {
      EXPECT_EQ('\0', p[i]);
    }
    free(p);
  }
}


// A global test counter for number of times the NewHandler is called.
static int news_handled = 0;
static void TestNewHandler() {
  ++news_handled;
  throw std::bad_alloc();
}

// Because we compile without exceptions, we expect these will not throw.
static void TestOneNewWithoutExceptions(void* (*func)(size_t),
                                        bool should_throw) {
  // success test
  try {
    void* ptr = (*func)(kNotTooBig);
    EXPECT_NE(reinterpret_cast<void*>(NULL), ptr) <<
        "allocation should not have failed.";
  } catch(...) {
    EXPECT_EQ(0, 1) << "allocation threw unexpected exception.";
  }

  // failure test
  try {
    void* rv = (*func)(kTooBig);
    EXPECT_EQ(NULL, rv);
    EXPECT_FALSE(should_throw) << "allocation should have thrown.";
  } catch(...) {
    EXPECT_TRUE(should_throw) << "allocation threw unexpected exception.";
  }
}

static void TestNothrowNew(void* (*func)(size_t)) {
  news_handled = 0;

  // test without new_handler:
  std::new_handler saved_handler = std::set_new_handler(0);
  TestOneNewWithoutExceptions(func, false);

  // test with new_handler:
  std::set_new_handler(TestNewHandler);
  TestOneNewWithoutExceptions(func, true);
  EXPECT_EQ(news_handled, 1) << "nothrow new_handler was not called.";
  std::set_new_handler(saved_handler);
}

}  // namespace

//-----------------------------------------------------------------------------

TEST(Atomics, AtomicIncrementWord) {
    TestAtomicIncrement<butil::subtle::AtomicWord>();
}

TEST(Atomics, AtomicIncrement32) {
    TestAtomicIncrement<butil::subtle::Atomic32>();
}

TEST(Atomics, AtomicOpsWord) {
    TestAtomicIncrement<butil::subtle::AtomicWord>();
}

TEST(Atomics, AtomicOps32) {
    TestAtomicIncrement<butil::subtle::Atomic32>();
}

TEST(Allocators, Malloc) {
  // Try allocating data with a bunch of alignments and sizes
  for (int size = 1; size < 1048576; size *= 2) {
    unsigned char* ptr = reinterpret_cast<unsigned char*>(malloc(size));
    CheckAlignment(ptr, 2);  // Should be 2 byte aligned
    Fill(ptr, size);
    EXPECT_TRUE(Valid(ptr, size));
    free(ptr);
  }
}

TEST(Allocators, Calloc) {
  TestCalloc(0, 0, true);
  TestCalloc(0, 1, true);
  TestCalloc(1, 1, true);
  TestCalloc(1<<10, 0, true);
  TestCalloc(1<<20, 0, true);
  TestCalloc(0, 1<<10, true);
  TestCalloc(0, 1<<20, true);
  TestCalloc(1<<20, 2, true);
  TestCalloc(2, 1<<20, true);
  TestCalloc(1000, 1000, true);

  // Not work in glib 2.12 (Red Hat 4.4.6-3, Linux 2.6.32)
  // TestCalloc(kMaxSize, 2, false);
  // TestCalloc(2, kMaxSize, false);
  // TestCalloc(kMaxSize, kMaxSize, false);

  // TestCalloc(kMaxSignedSize, 3, false);
  // TestCalloc(3, kMaxSignedSize, false);
  // TestCalloc(kMaxSignedSize, kMaxSignedSize, false);
}

TEST(Allocators, New) {
  TestNothrowNew(&::operator new);
  TestNothrowNew(&::operator new[]);
}

// This makes sure that reallocing a small number of bytes in either
// direction doesn't cause us to allocate new memory.
TEST(Allocators, Realloc1) {
  int start_sizes[] = { 100, 1000, 10000, 100000 };
  int deltas[] = { 1, -2, 4, -8, 16, -32, 64, -128 };

  for (size_t s = 0; s < sizeof(start_sizes)/sizeof(*start_sizes); ++s) {
    void* p = malloc(start_sizes[s]);
    ASSERT_TRUE(p);
    // The larger the start-size, the larger the non-reallocing delta.
    for (size_t d = 0; d < s*2; ++d) {
      void* new_p = realloc(p, start_sizes[s] + deltas[d]);
      ASSERT_EQ(p, new_p);  // realloc should not allocate new memory
    }
    // Test again, but this time reallocing smaller first.
    for (size_t d = 0; d < s*2; ++d) {
      void* new_p = realloc(p, start_sizes[s] - deltas[d]);
      ASSERT_EQ(p, new_p);  // realloc should not allocate new memory
    }
    free(p);
  }
}

TEST(Allocators, Realloc2) {
  for (int src_size = 0; src_size >= 0; src_size = NextSize(src_size)) {
    for (int dst_size = 0; dst_size >= 0; dst_size = NextSize(dst_size)) {
      unsigned char* src = reinterpret_cast<unsigned char*>(malloc(src_size));
      Fill(src, src_size);
      unsigned char* dst =
          reinterpret_cast<unsigned char*>(realloc(src, dst_size));
      EXPECT_TRUE(Valid(dst, min(src_size, dst_size)));
      Fill(dst, dst_size);
      EXPECT_TRUE(Valid(dst, dst_size));
      if (dst != NULL) free(dst);
    }
  }

  // Now make sure realloc works correctly even when we overflow the
  // packed cache, so some entries are evicted from the cache.
  // The cache has 2^12 entries, keyed by page number.
  const int kNumEntries = 1 << 14;
  int** p = reinterpret_cast<int**>(malloc(sizeof(*p) * kNumEntries));
  int sum = 0;
  for (int i = 0; i < kNumEntries; i++) {
    // no page size is likely to be bigger than 8192?
    p[i] = reinterpret_cast<int*>(malloc(8192));
    p[i][1000] = i;              // use memory deep in the heart of p
  }
  for (int i = 0; i < kNumEntries; i++) {
    p[i] = reinterpret_cast<int*>(realloc(p[i], 9000));
  }
  for (int i = 0; i < kNumEntries; i++) {
    sum += p[i][1000];
    free(p[i]);
  }
  EXPECT_EQ(kNumEntries/2 * (kNumEntries - 1), sum);  // assume kNE is even
  free(p);
}

TEST(Allocators, ReallocZero) {
  // Test that realloc to zero does not return NULL.
  for (int size = 0; size >= 0; size = NextSize(size)) {
    char* ptr = reinterpret_cast<char*>(malloc(size));
    EXPECT_NE(static_cast<char*>(NULL), ptr);
    ptr = reinterpret_cast<char*>(realloc(ptr, 0));
    EXPECT_NE(static_cast<char*>(NULL), ptr);
    if (ptr)
      free(ptr);
  }
}

#ifdef WIN32
// Test recalloc
TEST(Allocators, Recalloc) {
  for (int src_size = 0; src_size >= 0; src_size = NextSize(src_size)) {
    for (int dst_size = 0; dst_size >= 0; dst_size = NextSize(dst_size)) {
      unsigned char* src =
          reinterpret_cast<unsigned char*>(_recalloc(NULL, 1, src_size));
      EXPECT_TRUE(IsZeroed(src, src_size));
      Fill(src, src_size);
      unsigned char* dst =
          reinterpret_cast<unsigned char*>(_recalloc(src, 1, dst_size));
      EXPECT_TRUE(Valid(dst, min(src_size, dst_size)));
      Fill(dst, dst_size);
      EXPECT_TRUE(Valid(dst, dst_size));
      if (dst != NULL)
        free(dst);
    }
  }
}

// Test windows specific _aligned_malloc() and _aligned_free() methods.
TEST(Allocators, AlignedMalloc) {
  // Try allocating data with a bunch of alignments and sizes
  static const int kTestAlignments[] = {8, 16, 256, 4096, 8192, 16384};
  for (int size = 1; size > 0; size = NextSize(size)) {
    for (int i = 0; i < ARRAYSIZE(kTestAlignments); ++i) {
      unsigned char* ptr = static_cast<unsigned char*>(
          _aligned_malloc(size, kTestAlignments[i]));
      CheckAlignment(ptr, kTestAlignments[i]);
      Fill(ptr, size);
      EXPECT_TRUE(Valid(ptr, size));

      // Make a second allocation of the same size and alignment to prevent
      // allocators from passing this test by accident.  Per jar, tcmalloc
      // provides allocations for new (never before seen) sizes out of a thread
      // local heap of a given "size class."  Each time the test requests a new
      // size, it will usually get the first element of a span, which is a
      // 4K aligned allocation.
      unsigned char* ptr2 = static_cast<unsigned char*>(
          _aligned_malloc(size, kTestAlignments[i]));
      CheckAlignment(ptr2, kTestAlignments[i]);
      Fill(ptr2, size);
      EXPECT_TRUE(Valid(ptr2, size));

      // Should never happen, but sanity check just in case.
      ASSERT_NE(ptr, ptr2);
      _aligned_free(ptr);
      _aligned_free(ptr2);
    }
  }
}

#endif
