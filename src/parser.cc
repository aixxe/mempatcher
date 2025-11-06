#include <format>
#include <ranges>
#include <fstream>
#include <charconv>

#include "parser.h"

using namespace mempatcher;
using namespace mempatcher::parser;

namespace mempatcher::parser::detail
{
    struct error_category final: std::error_category
    {
        [[nodiscard]] auto name() const noexcept -> const char* override;
        [[nodiscard]] auto message(int c) const -> std::string override;
    };
}

auto const error_category_instance = detail::error_category {};

auto patch::type_name() const -> std::string_view
{
    switch (type)
    {
        case addr_type::absolute: return "absolute address";
        case addr_type::rva:      return "RVA";
        case addr_type::file:     return "file";
    }

    return "???";
}

auto patch::target_name() const -> std::string
{
    if (this->type == addr_type::absolute)
        return std::format("0x{:X}", this->address);

    return std::format("'{}'+0x{:X}", this->target, this->address);
}

auto parser::make_error_code(errc e) -> std::error_code
    { return { static_cast<int>(e), error_category_instance }; }

auto detail::error_category::name() const noexcept -> const char*
    { return "parser_error"; }

auto detail::error_category::message(int c) const -> std::string
{
    switch (static_cast<errc>(c))
    {
        case errc::file_not_found:
            return "The input file does not exist";
        case errc::file_open_fail:
            return "The input file could not be opened";
        case errc::parse_line_empty:
            return "The line was empty or a comment";
        case errc::parse_insufficient_args:
            return "The line does not contain enough arguments";
        case errc::parse_too_many_args:
            return "The line contains too many arguments";
        case errc::parse_target_unclosed_quote:
            return "The target filename contains an unclosed quote";
        case errc::parse_bad_offset_address:
            return "The offset is not a valid hexadecimal number";
        case errc::parse_bad_data_length:
            return "The data length is not a multiple of 2";
        case errc::parse_bad_data_bytes:
            return "The data contains invalid bytes";
        default:
            return "???";
    }
}

/**
 * Read the target filename from a patch line.
 *
 * @param line The full line to read from.
 * @return The target if successful, otherwise an error code.
 */
auto parser::read_target(const std::string& line)
    -> std::expected<read_target_result, errc>
{
    auto const quoted = line.starts_with('"');
    auto const start = quoted ? 1: 0;
    auto const end = line.find(quoted ? '"': ' ', start);

    if (end == std::string::npos)
        return std::unexpected { quoted ? errc::parse_target_unclosed_quote:
                                          errc::parse_insufficient_args };

    return read_target_result {
        .name = line.substr(start, end - start),
        .offset = end + 1
    };
}

/**
 * Read offset to target where the patch should be applied.
 *
 * @param offset The offset component from the line.
 * @return The offset if successful, otherwise an error code.
 */
auto parser::read_offset(const std::string& offset)
    -> std::expected<read_offset_result, errc>
{
    auto const file = offset.starts_with("f+") || offset.starts_with("F+");
    auto const start = file ? 2: 0;

    auto result = read_offset_result
        { .type = file ? addr_type::file:
                         addr_type::rva };

    auto const [_, ec] = std::from_chars(offset.data() + start,
        offset.data() + offset.size(), result.address, 16);

    if (ec != std::errc {})
        return std::unexpected { errc::parse_bad_offset_address };

    return result;
}

/**
 * Convert hex patch data to a vector of bytes.
 *
 * @param bytes A data component from the line. (e.g. "112233445566")
 * @return A vector of bytes if successful, otherwise an error code.
 */
auto parser::read_data(const std::string& bytes)
    -> std::expected<std::vector<std::uint8_t>, errc>
{
    if (bytes.size() % 2 != 0)
        return std::unexpected { errc::parse_bad_data_length };

    auto result = std::vector<std::uint8_t>(bytes.size() / 2);

    for (auto i = 0; i < bytes.size(); i += 2)
    {
        auto const [_, ec] = std::from_chars(bytes.data() + i,
            bytes.data() + i + 2, result[i / 2], 16);

        if (ec != std::errc {})
            return std::unexpected { errc::parse_bad_data_bytes };
    }

    return result;
}

/**
 * Parse a single line of a memory patch file.
 *
 * @param line The line to parse.
 * @return A patch if successful, otherwise an error code.
 */
auto parser::read_line(const std::string& line)
    -> std::expected<patch, errc>
{
    // ignore comments and empty lines
    if (line.starts_with('#') || line.empty())
        return std::unexpected { errc::parse_line_empty };

    // ignore lines that only contain spaces
    if (line.find_first_not_of(' ') == std::string::npos)
        return std::unexpected { errc::parse_line_empty };

    // handle target first in case it contains spaces
    auto target = read_target(line);

    if (!target)
        return std::unexpected { target.error() };

    auto result = patch { .target = std::move(target->name) };

    // slice off target and split the rest at spaces
    auto const args = line
        | std::views::drop(target->offset)
        | std::views::split(' ')
        | std::views::filter([] (auto&& v) { return !v.empty(); })
        | std::ranges::to<std::vector<std::string>>();

    // need at least an offset and some bytes to apply
    if (args.size() < 2)
        return std::unexpected { errc::parse_insufficient_args };

    // maximum amount of arguments should be 3 now
    if (args.size() > 3)
        return std::unexpected { errc::parse_too_many_args };

    // first part is always the offset
    auto offset = read_offset(args[0]);

    if (!offset)
        return std::unexpected { offset.error() };

    result.type = offset->type;
    result.address = offset->address;

    if (result.target == "-")
        result.type = addr_type::absolute;

    // second is 'on' bytes, can be '-' for no change
    if (args[1] != "-")
    {
        if (auto on = read_data(args[1]))
            result.on = std::move(*on);
        else
            return std::unexpected { on.error() };
    }

    // third is 'off' bytes, optional
    if (args.size() > 2)
    {
        if (auto off = read_data(args[2]))
            result.off = std::move(*off);
        else
            return std::unexpected { off.error() };
    }

    // ensure there's something to do
    if (result.on.empty() && result.off.empty())
        return std::unexpected { errc::parse_insufficient_args };

    return result;
}

/**
 * Read memory patches from a file.
 *
 * @param path The path to the file to read.
 * @return A vector of patches if successful, otherwise an error code.
 */
auto parser::read_file(const std::filesystem::path& path)
    -> std::expected<std::vector<patch>, parse_error>
{
    if (!exists(path))
        return std::unexpected { parse_error { errc::file_not_found, 0 } };

    auto file = std::ifstream { path };

    if (!file)
        return std::unexpected { parse_error { errc::file_open_fail, 0 } };

    auto nline = std::size_t { 1 };
    auto result = std::vector<patch> {};

    for (auto line = std::string {}; std::getline(file, line); ++nline)
    {
        auto patch = read_line(line);

        if (!patch && patch.error() == errc::parse_line_empty)
            continue;

        if (!patch)
            return std::unexpected { parse_error { patch.error(), nline } };

        patch->line = nline;
        patch->file = path.filename().string();

        result.push_back(std::move(*patch));
    }

    return result;
}