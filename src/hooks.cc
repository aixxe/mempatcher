#include <ranges>
#include <algorithm>

#include "util.h"
#include "hooks.h"
#include "patch.h"

using namespace mempatcher;
using namespace mempatcher::hooks;

namespace mempatcher::hooks::detail
{
    auto cookie = PVOID {};
    auto module = HMODULE {};
    auto patches = patch_list {};

    decltype(LdrUnregisterDllNotification)* unregister_fn {};
}

/**
 * Unload module from process.
 */
auto WINAPI unload(PVOID = nullptr) -> DWORD
{
    FreeLibraryAndExitThread(detail::module, EXIT_SUCCESS);
}

/**
 * Unregister loader notification and unload module from process.
 */
auto WINAPI unregister_and_unload(PVOID) -> DWORD
{
    auto const result = detail::unregister_fn(detail::cookie);

    if (NT_SUCCESS(result))
        return unload();

    spdlog::error("Failed to unregister DLL notification callback (0x{:X})", result);
    return EXIT_FAILURE;
}

/**
 * Handle newly loaded DLLs and apply patches.
 */
auto CALLBACK dll_notification(ULONG reason, PCLDR_DLL_NOTIFICATION_DATA data, PVOID) -> void
{
    if (detail::patches.empty() || reason != LDR_DLL_NOTIFICATION_REASON_LOADED)
        return;

    auto const address = static_cast<std::uint8_t*>(data->Loaded.DllBase);
    auto const module = util::narrow({
        data->Loaded.BaseDllName->Buffer,
        data->Loaded.BaseDllName->Length / sizeof(wchar_t)
    });

    auto const has_patches = std::ranges::any_of(detail::patches, [&] (auto&& patch)
        { return patch.target == module; });

    if (!has_patches)
        return;

    spdlog::info("Loaded target file '{}' at address 0x{:X}",
        module, std::bit_cast<std::uintptr_t>(address));

    auto applied = std::vector<patch_list::size_type> {};

    for (auto i = 0; i < detail::patches.size(); ++i)
    {
        if (detail::patches[i].target != module)
            continue;

        if (!patch::apply(address, detail::patches[i]))
            break;

        applied.emplace_back(i);
    }

    for (auto&& i: applied | std::views::reverse)
        detail::patches.erase(detail::patches.begin() + i);

    if (!detail::patches.empty())
        return;

    spdlog::info("All patches applied, unloading from process...");
    CreateThread(nullptr, 0, unregister_and_unload, nullptr, 0, nullptr);
}

/**
 * Attempts to apply patches if the target module is already loaded.
 */
auto apply_if_loaded(parser::patch& patch) -> std::optional<bool>
{
    if (patch.target == "-")
        return patch::apply(nullptr, patch);

    if (patch.target == "<host>")
    {
        auto const address = GetModuleHandle(nullptr);
        return patch::apply(reinterpret_cast<std::uint8_t*>(address), patch);
    }

    if (auto const handle = GetModuleHandleA(patch.target.c_str()))
    {
        auto static seen = std::vector<decltype(patch.target)> {};

        if (std::ranges::find(seen, patch.target) == seen.end())
        {
            spdlog::info("Target module '{}' resolved to address 0x{:X}",
                patch.target, std::bit_cast<std::uintptr_t>(handle));

            seen.push_back(patch.target);
        }

        return patch::apply(reinterpret_cast<std::uint8_t*>(handle), patch);
    }

    return std::nullopt;
}

/**
 * Installs notifications for loader events.
 *
 * @param module Handle to this module.
 * @param patches List of patches to apply.
 * @return True if the module should remain loaded, false otherwise.
 */
auto hooks::install(HMODULE module, patch_list&& patches) -> bool
{
    detail::module = module;
    detail::patches = std::move(patches);

    // apply patches for any libraries that are already loaded
    auto applied = std::vector<patch_list::size_type> {};

    for (auto i = 0; i < detail::patches.size(); ++i)
    {
        auto const result = apply_if_loaded(detail::patches[i]);

        if (!result.has_value())
            continue; // not loaded yet

        if (!*result)
            return false; // failed to apply

        applied.emplace_back(i); // applied
    }

    for (auto&& i: applied | std::views::reverse)
        detail::patches.erase(detail::patches.begin() + i);

    // if everything was applied, unload now
    if (detail::patches.empty())
    {
        spdlog::info("All patches applied, unloading from process...");
        CreateThread(nullptr, 0, unload, nullptr, 0, nullptr);
        return true;
    }

    // catch future libraries
    auto const imports = util::resolve_dll_imports("ntdll.dll",
        { "LdrRegisterDllNotification", "LdrUnregisterDllNotification" });

    if (imports.empty()) [[unlikely]]
    {
        spdlog::error("Failed to resolve required imports from 'ntdll.dll'");
        return false;
    }

    auto const register_fn = reinterpret_cast<decltype(&LdrRegisterDllNotification)>
        (imports.at("LdrRegisterDllNotification"));

    detail::unregister_fn = reinterpret_cast<decltype(&LdrUnregisterDllNotification)>
        (imports.at("LdrUnregisterDllNotification"));

    auto const result = register_fn(0, dll_notification, nullptr, &detail::cookie);

    if (result < 0)
        spdlog::error("Failed to register DLL notification callback (code: {:X})", result);

    return NT_SUCCESS(result);
}