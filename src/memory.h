#pragma once

#include "parser.h"

namespace mempatcher::memory
{
    [[nodiscard]] auto compare(std::uint8_t* target, const std::vector<std::uint8_t>& data) -> bool;
}
