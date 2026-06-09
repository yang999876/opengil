#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace opengil {

std::string sha256_hex(std::span<const uint8_t> bytes);

}  // namespace opengil

