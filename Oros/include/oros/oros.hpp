/* Copyright (c) 2026 Otto Link. Distributed under the terms of the GNU General
   Public License. The full license is in the file LICENSE, distributed with
   this software. */

#pragma once

#include <string_view>

namespace oros
{

// --- Version and library info

[[nodiscard]] std::string_view get_version();

} // namespace oros
