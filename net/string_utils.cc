/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "net/checks.h"
#include "net/string_utils.h"

namespace net {

bool memory_check(const void* memory, int c, size_t count) {
  const char* char_memory = static_cast<const char*>(memory);
  char char_c = static_cast<char>(c);
  for (size_t i = 0; i < count; ++i) {
    if (char_memory[i] != char_c) {
      return false;
    }
  }
  return true;
}

bool string_match(const char* target, const char* pattern) {
  while (*pattern) {
    if (*pattern == '*') {
      if (!*++pattern) {
        return true;
      }
      while (*target) {
        if ((toupper(*pattern) == toupper(*target))
            && string_match(target + 1, pattern + 1)) {
          return true;
        }
        ++target;
      }
      return false;
    } else {
      if (toupper(*pattern) != toupper(*target)) {
        return false;
      }
      ++target;
      ++pattern;
    }
  }
  return !*target;
}

void replace_substrs(const char *search,
                     size_t search_len,
                     const char *replace,
                     size_t replace_len,
                     std::string *s) {
  size_t pos = 0;
  while ((pos = s->find(search, pos, search_len)) != std::string::npos) {
    s->replace(pos, search_len, replace, replace_len);
    pos += replace_len;
  }
}

bool starts_with(const char *s1, const char *s2) {
  return strncmp(s1, s2, strlen(s2)) == 0;
}

bool ends_with(const char *s1, const char *s2) {
  size_t s1_length = strlen(s1);
  size_t s2_length = strlen(s2);

  if (s2_length > s1_length) {
    return false;
  }

  const char* start = s1 + (s1_length - s2_length);
  return strncmp(start, s2, s2_length) == 0;
}

static const char kWhitespace[] = " \n\r\t";

std::string string_trim(const std::string& s) {
  std::string::size_type first = s.find_first_not_of(kWhitespace);
  std::string::size_type last  = s.find_last_not_of(kWhitespace);

  if (first == std::string::npos || last == std::string::npos) {
    return std::string("");
  }

  return s.substr(first, last - first + 1);
}

}  // namespace rtc
