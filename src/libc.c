#include <libc.h>

// Copied from libsanitizer
char *internal_strstr(const char *haystack, const char *needle) {
  // This is O(N^2), but we are not using it in hot places.
  size_t len1 = internal_strlen(haystack);
  size_t len2 = internal_strlen(needle);
  if (len1 < len2) return 0;
  size_t pos;
  for (pos = 0; pos <= len1 - len2; pos++) {
    if (internal_memcmp(haystack + pos, needle, len2) == 0)
      return (char *)haystack + pos;
  }
  return 0;
}

const char *int2str(int value, char *str, size_t size) {
  int neg = value > 0;
  value = value >= 0 ? value : -value;

  if(!size)
    return 0;

  size_t cur = size - 1;
  str[cur] = 0;

  while(value > 0) {
    if(!cur)
      return 0;
    int digit = value % 10;
    value /= 10;
    str[--cur] = '0' + digit;
  }

  if(neg) {
    if(!cur)
      return 0;
    str[--cur] = '-';
  }

  return &str[cur];
}

