#include "base/location.h"
#include <stdio.h>

namespace base {

Location::Location(const char* function_name, const char* file_and_line)
    : function_name_(function_name), file_and_line_(file_and_line) {}

Location::Location() : function_name_("Unknown"), file_and_line_("Unknown") {}

Location::Location(const Location& other)
    : function_name_(other.function_name_),
      file_and_line_(other.file_and_line_) {}

Location& Location::operator=(const Location& other) {
  function_name_ = other.function_name_;
  file_and_line_ = other.file_and_line_;
  return *this;
}

std::string Location::ToString() const {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s@%s", function_name_, file_and_line_);
  return buf;
}

} // namespace base
