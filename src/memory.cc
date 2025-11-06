#include "memory.h"
#include <windows.h>

using namespace mempatcher;

/**
 * Compare two blocks of data in memory.
 *
 * @param target Pointer to the memory location.
 * @param data Vector of bytes to compare against.
 * @return True if the data matches, false otherwise.
 */
auto memory::compare(std::uint8_t* target, const std::vector<std::uint8_t>& data) -> bool
{
    if (data.empty())
        return false;

    auto info = MEMORY_BASIC_INFORMATION {};

    if (VirtualQuery(target, &info, sizeof(info)) == 0)
        return false;

    if (info.State != MEM_COMMIT)
        return false;

    auto const base = static_cast<std::uint8_t*>(info.BaseAddress);

    if (target + data.size() > base + info.RegionSize)
        return false;

    auto constexpr readable_protections = PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY |
        PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY;

    auto const is_readable = (info.Protect & readable_protections) != 0;

    auto old_protect = DWORD {};
    auto new_protect = DWORD { PAGE_READONLY };
    
    if (!is_readable)
    {
        if (info.Protect & PAGE_EXECUTE)
            new_protect = PAGE_EXECUTE_READ;

        if (VirtualProtect(target, data.size(), new_protect, &old_protect) == 0)
            return false;
    }

    auto const result = std::memcmp(target, data.data(), data.size()) == 0;

    if (!is_readable && old_protect != 0)
        VirtualProtect(target, data.size(), old_protect, &new_protect);

    return result;
}