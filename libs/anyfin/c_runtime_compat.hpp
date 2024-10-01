
#pragma once

/*
  This file must be included only once into whatever the main file of the project is to avoid linking issues.
 */

extern "C" {

#pragma function(memset)
void* memset(void* ptr, int c, size_t n) {
  char* p = (char*)ptr;
  for (size_t i = 0; i < n; i++) {
    p[i] = (char)c;
  }
  return ptr;
}

#pragma function(memcpy)
void* memcpy(void* dest, const void* src, size_t n) {
  char* d = (char*)dest;
  const char* s = (const char*)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}

#pragma function(memcmp)
int memcmp(const void* s1, const void* s2, size_t n) {
  const unsigned char* p1 = (const unsigned char*)s1;
  const unsigned char* p2 = (const unsigned char*)s2;
  for (size_t i = 0; i < n; i++) { 
    if (p1[i] != p2[i]) {
      return p1[i] - p2[i];
    }
  }
  return 0;
}

}
