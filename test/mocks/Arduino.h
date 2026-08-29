#pragma once

// Minimal Arduino.h shim for native (host) unit tests.
// Provides the millis()/delay() clock mocks used by existing tests, plus the
// string helpers and inert File / FILESYSTEM stubs needed by the headers pulled
// in by RegionMap / TransportKeyStore / IdentityStore so they compile off-device.
// The region-gating tests exercise in-memory logic and never touch the filesystem.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

// The real Arduino.h pulls in stdlib.h, so device code reaches atoi/atol/atof/strtoul
// without including it. <cstdlib> is already included above; these using-decls are
// what device sources actually need, or they fail only on the native build.
#include "Stream.h"

using std::atof;
using std::atoi;
using std::atol;
using std::isnan;

inline uint32_t g_mock_millis = 0;

inline uint32_t millis() {
  return g_mock_millis;
}

inline void delay(uint32_t ms) {
  g_mock_millis += ms;
}

// ---- Arduino string helpers not present in the host libc --------------------
// (MinGW/Windows already declares ltoa in <stdlib.h>, so only define it where
//  the host libc lacks it — e.g. glibc on Linux.)
#if !defined(_WIN32) && !defined(__MINGW32__)
static inline char* ltoa(long value, char* result, int base) {
  if (base < 2 || base > 36) { *result = '\0'; return result; }
  char* ptr = result;
  char* low = result;
  if (value < 0 && base == 10) { *ptr++ = '-'; low = ptr; }
  unsigned long uval = (value < 0 && base == 10) ? (unsigned long)(-value) : (unsigned long)value;
  do {
    int digit = (int)(uval % (unsigned long)base);
    *ptr++ = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    uval /= (unsigned long)base;
  } while (uval);
  *ptr-- = '\0';
  while (low < ptr) { char t = *low; *low++ = *ptr; *ptr-- = t; }
  return result;
}
#endif

// ---- filesystem stubs -------------------------------------------------------
class File {
public:
  operator bool() const { return false; }
  int read(uint8_t* buf, size_t len) { (void)buf; (void)len; return 0; }
  size_t write(const uint8_t* buf, size_t len) { (void)buf; (void)len; return 0; }
  void close() {}
};

class FsMock {
public:
  bool exists(const char* path) { (void)path; return false; }
  bool remove(const char* path) { (void)path; return false; }
  bool mkdir(const char* path) { (void)path; return false; }
  File open(const char* path) { (void)path; return File(); }
  File open(const char* path, const char* mode) { (void)path; (void)mode; return File(); }
  File open(const char* path, const char* mode, bool create) {
    (void)path; (void)mode; (void)create; return File();
  }
};

#ifndef FILESYSTEM
  #define FILESYSTEM FsMock
#endif
