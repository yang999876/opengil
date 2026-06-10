#include "test_support.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "opengil/sha256.hpp"
#include "opengil/wire.hpp"

int main() {
  using namespace opengil;

  const auto encoded = encode_varint(300);
  OPENGIL_CHECK(encoded.size() == 2);
  OPENGIL_CHECK(encoded[0] == 0xac);
  OPENGIL_CHECK(encoded[1] == 0x02);

  const auto decoded = read_varint(std::span<const uint8_t>(encoded.data(), encoded.size()), 0);
  OPENGIL_CHECK(decoded);
  OPENGIL_CHECK(decoded->value == 300);
  OPENGIL_CHECK(decoded->next == 2);

  const std::vector<uint8_t> payload = {0x08, 0x96, 0x01, 0x12, 0x03, 'a', 'b', 'c'};
  std::vector<Field> fields;
  std::string error;
  OPENGIL_CHECK(parse_fields(payload, fields, &error));
  OPENGIL_CHECK(fields.size() == 2);
  OPENGIL_CHECK(fields[0].number == 1);
  OPENGIL_CHECK(fields[0].wire == 0);
  OPENGIL_CHECK(fields[0].varint == 150);
  OPENGIL_CHECK(fields[1].number == 2);
  OPENGIL_CHECK(fields[1].wire == 2);

  const std::string hash = sha256_hex(std::span<const uint8_t>());
  OPENGIL_CHECK(hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  return 0;
}

