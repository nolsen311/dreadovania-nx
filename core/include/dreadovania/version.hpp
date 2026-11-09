// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#ifndef DREADOVANIA_VERSION
#error "DREADOVANIA_VERSION must be defined by the build (from the VERSION file)"
#endif

namespace dreadovania {
inline constexpr std::string_view kVersion = DREADOVANIA_VERSION;
}  // namespace dreadovania
