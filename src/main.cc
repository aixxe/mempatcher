#include <ranges>

#include "util.h"
#include "hooks.h"
#include "buildinfo.h"

using namespace mempatcher;

/**
 * Find input patch files from the process command line.
 * Support both '--mempatch <file>' and '--mempatch=<file>' formats.
 *
 * @param argv Command line arguments.
 * @return List of input files.
 */
auto get_input_files(const std::vector<std::string>& argv)
{
    auto result = std::vector<std::filesystem::path> {};

    for (auto i = 1; i < argv.size(); ++i)
    {
        auto path = std::filesystem::path {};

        if (argv[i].starts_with("--mempatch="))
            path = { argv[i].substr(11) };
        else if (argv[i] == "--mempatch" && ++i < argv.size())
            path = { argv[i] };

        if (path.empty())
            continue;

        if (!exists(path))
        {
            spdlog::warn("Patch file '{}' does not exist!", path.string());
            continue;
        }

        result.push_back(std::move(path));
    }

    return result;
}

/**
 * Find *.mph files in a given directory.
 *
 * @return List of input files.
 */
auto get_directory_files(const std::filesystem::path& path)
{
    auto result = std::vector<std::filesystem::path> {};

    if (!is_directory(path))
        return result;

    for (auto&& entry: std::filesystem::directory_iterator(path))
        if (entry.is_regular_file() && entry.path().extension() == ".mph")
            result.push_back(entry.path());

    return result;
}

/**
 * Get the base filename of the host module.
 *
 * @return Host module filename.
 */
auto get_host_exe() -> std::string
{
    auto name = std::wstring(MAX_PATH, L'\0');
    auto const size = GetModuleFileNameW(nullptr, name.data(), name.size());

    if (size == 0)
        return "<unknown>";

    name.resize(size);

    return std::filesystem::path { name }.filename().string();
}

/**
 * Main entrypoint: install hooks & patch any already loaded libraries.
 *
 * @param module Handle to this module.
 * @param reason Reason for calling this function.
 * @return True if the module should remain loaded, false otherwise.
 */
auto WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) -> BOOL
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    DisableThreadLibraryCalls(module);

    spdlog::default_logger()->sinks() =
        { std::make_shared<spdlog::sinks::basic_file_sink_st>("mempatcher.log", true) };
    spdlog::default_logger()->flush_on(spdlog::level::info);
    spdlog::default_logger()->set_pattern("[%Y/%m/%d %H:%M:%S] %v");

    spdlog::info("mempatcher (v{}) loaded in '{}' at 0x{:X}",
        RC_FILEVERSION_STRING, get_host_exe(), std::bit_cast<std::uintptr_t>(module));
    spdlog::info("Compiled at {} {} from {}@{}",
        __DATE__, __TIME__, GIT_BRANCH_NAME, GIT_COMMIT_HASH_SHORT);

    auto const argv = util::get_argv();
    auto files = get_input_files(argv);

    std::ranges::move(get_directory_files("autopatch"), std::back_inserter(files));

    if (argv.size() > 1)
        spdlog::info("Command line arguments: {}", fmt::join(std::views::drop(argv, 1), " "));

    if (files.empty())
    {
        spdlog::warn("No patch files could be found in the command line arguments");
        spdlog::warn("Use one or more '--mempatch <file>' arguments to specify paths to patch files");

        return FALSE;
    }

    auto patches = hooks::patch_list {};

    for (auto&& file: files)
    {
        spdlog::info("Opening memory patch file '{}'...", file.string());

        auto result = parser::read_file(file);

        if (!result)
        {
            spdlog::error("Parsing failed on line {}: {}",
                result.error().line, make_error_code(result.error().ec).message());

            return FALSE;
        }

        for (auto&& patch: *result)
        {
            spdlog::info("Parsed {} from '{}':{} at {} {}",
                patch.on.empty() ? "check": "patch", patch.file,
                patch.line, patch.type_name(), patch.target_name());

            if (!patch.off.empty())
                spdlog::info("     expected data [{}]: {:02X}", patch.off.size(), fmt::join(patch.off, " "));

            if (!patch.on.empty())
                spdlog::info("  replacement data [{}]: {:02X}", patch.on.size(), fmt::join(patch.on, " "));

            patches.push_back(std::move(patch));
        }
    }

    // set up hooks
    if (!hooks::install(module, std::move(patches)))
        return FALSE;

    return TRUE;
}