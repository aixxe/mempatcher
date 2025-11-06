#include <vector>
#include <cstdint>
#include <windows.h>
#include <catch2/catch_test_macros.hpp>

#include "../src/memory.h"

using namespace mempatcher;

TEST_CASE("Comparing identical data returns true", "[compare]")
{
    auto a = std::vector<std::uint8_t> { 0x11, 0x22, 0x33, 0x44, 0x55 };
    auto b = std::vector<std::uint8_t> { 0x11, 0x22, 0x33, 0x44, 0x55 };
    
    REQUIRE(memory::compare(a.data(), b) == true);
}

TEST_CASE("Comparing different data returns false", "[compare]")
{
    auto a = std::vector<std::uint8_t> { 0x11, 0x22, 0x33, 0x44, 0x55 };
    auto b = std::vector<std::uint8_t> { 0x11, 0x22, 0xFF, 0x44, 0x55 };
    
    REQUIRE(memory::compare(a.data(), b) == false);
}

TEST_CASE("Comparing with empty data returns false", "[compare]")
{
    auto a = std::vector<std::uint8_t> { 0x11, 0x22, 0x33 };
    auto b = std::vector<std::uint8_t> {};
    
    REQUIRE(memory::compare(a.data(), b) == false);
}

TEST_CASE("Comparing succeeds with large arrays", "[compare]")
{
    auto constexpr size = 1024 * 1024 * 32; // 32 MB

    auto a = std::vector<std::uint8_t>(size);
    auto b = std::vector<std::uint8_t>(size);
    
    for (auto i = 0; i < a.size(); ++i)
    {
        a[i] = static_cast<std::uint8_t>(i & 0xFF);
        b[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    
    REQUIRE(memory::compare(a.data(), b) == true);
    
    b[512] = 0xFF;
    REQUIRE(memory::compare(a.data(), b) == false);
}

TEST_CASE("Comparing with invalid pointer returns false", "[compare]")
{
    auto a = std::vector<std::uint8_t> { 0x11, 0x22, 0x33 };
    
    REQUIRE(memory::compare(nullptr, a) == false);
    REQUIRE(memory::compare(reinterpret_cast<std::uint8_t*>(0x1337CAFE), a) == false);
}

TEST_CASE("Comparing against inaccessible memory returns true", "[compare]")
{
    auto a = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto b = std::vector<std::uint8_t> { 0xAA, 0xBB, 0xCC, 0xDD };

    REQUIRE(a != nullptr);
    
    a[0] = 0xAA;
    a[1] = 0xBB;
    a[2] = 0xCC;
    a[3] = 0xDD;

    REQUIRE(memory::compare(a, b) == true);
    
    auto old_protect = DWORD {};
    REQUIRE(VirtualProtect(a, 4096, PAGE_NOACCESS, &old_protect) != 0);
    REQUIRE(memory::compare(a, b) == true);
    
    auto mbi = MEMORY_BASIC_INFORMATION {};
    VirtualQuery(a, &mbi, sizeof(mbi));
    REQUIRE(mbi.Protect == PAGE_NOACCESS);
    VirtualFree(a, 0, MEM_RELEASE);
}

TEST_CASE("Comparing against execute-only memory returns true", "[compare]")
{
    auto a = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto b = std::vector<std::uint8_t> { 0x90, 0x90, 0xC3 };

    REQUIRE(a != nullptr);

    a[0] = 0x90;
    a[1] = 0x90;
    a[2] = 0xC3;

    REQUIRE(memory::compare(a, b) == true);
    
    auto old_protect = DWORD {};
    REQUIRE(VirtualProtect(a, 4096, PAGE_EXECUTE, &old_protect) != 0);
    REQUIRE(memory::compare(a, b) == true);
    
    auto mbi = MEMORY_BASIC_INFORMATION {};
    VirtualQuery(a, &mbi, sizeof(mbi));
    REQUIRE(mbi.Protect == PAGE_EXECUTE);
    VirtualFree(a, 0, MEM_RELEASE);
}

TEST_CASE("Comparison across region boundary returns false", "[compare]")
{
    auto a = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto b = std::vector<std::uint8_t>(4096, 0xAA);

    REQUIRE(a != nullptr);
    std::memset(a, 0xAA, 4096);

    REQUIRE(memory::compare(a + 2048, b) == false);
    VirtualFree(a, 0, MEM_RELEASE);
}

TEST_CASE("Comparing against freed memory returns false", "[compare]")
{
    auto a = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto b = std::vector<std::uint8_t> { 0xAA, 0xBB, 0xCC };

    REQUIRE(a != nullptr);
    
    VirtualFree(a, 0, MEM_RELEASE);
    REQUIRE(memory::compare(a, b) == false);
}