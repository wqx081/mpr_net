#ifndef BASE_LOCATION_H_
#define BASE_LOCATION_H_
#include <string>

namespace base {

class Location {
 public:
  Location(const char* function_name, const char* file_and_line);
  Location();
  Location(const Location& other);
  Location& operator=(const Location& other);
  
  const char* function_name() const { return function_name_; }
  const char* file_and_line() const { return file_and_line_; }
  
  std::string ToString() const;
  
 private:
  const char* function_name_;
  const char* file_and_line_;
};

#define XX(x) #x
#define YY(x) XX(x)

#define FROM_HERE FROM_HERE_WITH_FUNCTION(__FUNCTION__)
  
#define FROM_HERE_WITH_FUNCTION(function_name) \
  ::base::Location(function_name, __FILE__ ":" YY(__LINE__))

} // namespace base
#endif // BASE_LOCATION_H_
