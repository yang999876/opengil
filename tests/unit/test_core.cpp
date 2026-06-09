#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "opengil/sha256.hpp"
#include "opengil/wire.hpp"

int main() {
  using namespace opengil;

  const auto encoded = encode_varint(300);
  assert(encoded.size() == 2);
  assert(encoded[0] == 0xac);
  assert(encoded[1] == 0x02);

  const auto decoded = read_varint(std::span<const uint8_t>(encoded.data(), encoded.size()), 0);
  assert(decoded);
  assert(decoded->value == 300);
  assert(decoded->next == 2);

  const std::vector<uint8_t> payload = {0x08, 0x96, 0x01, 0x12, 0x03, 'a', 'b', 'c'};
  std::vector<Field> fields;
  std::string error;
  assert(parse_fields(payload, fields, &error));
  assert(fields.size() == 2);
  assert(fields[0].number == 1);
  assert(fields[0].wire == 0);
  assert(fields[0].varint == 150);
  assert(fields[1].number == 2);
  assert(fields[1].wire == 2);

  const std::string hash = sha256_hex(std::span<const uint8_t>());
  assert(hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  return 0;
}

