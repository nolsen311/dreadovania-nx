// SPDX-License-Identifier: GPL-3.0-or-later
// SHA-256 (FIPS 180-4), to check each downloaded file against the server's
// manifest. Portable, so the host tests use the same code as the Switch.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dreadovania {

class Sha256 {
  public:
    Sha256();
    void update(const void* data, std::size_t size);
    void update(std::string_view data) { update(data.data(), data.size()); }
    std::string hex();  // finishes the hash

  private:
    void block(const std::uint8_t* p);
    std::array<std::uint32_t, 8> h_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

}  // namespace dreadovania
