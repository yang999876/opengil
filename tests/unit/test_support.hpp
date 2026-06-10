#pragma once

#include <cstdlib>
#include <iostream>

namespace opengil::test {

inline void fail_check(const char *expression, const char *file, int line) {
  std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
  std::exit(EXIT_FAILURE);
}

}  // namespace opengil::test

#define OPENGIL_CHECK(...)                                                    \
  do {                                                                        \
    if (!static_cast<bool>((__VA_ARGS__))) {                                   \
      ::opengil::test::fail_check(#__VA_ARGS__, __FILE__, __LINE__);          \
    }                                                                         \
  } while (false)

#define OPENGIL_REQUIRE(...) OPENGIL_CHECK(__VA_ARGS__)
