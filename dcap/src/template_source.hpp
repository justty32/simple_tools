#pragma once
#include "builtin.hpp"
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dcap {

// A directory counts as a template iff it contains this file.
inline constexpr std::string_view kTemplateMarker = "CMakeLists.txt";

// Where a template's files come from: an embedded built-in, or a directory on
// disk (an external $DCAP_TEMPLATES entry, or a path given directly as the
// spec). Resolved once via resolve(), then materialised into the new project.
class TemplateSource {
public:
    // Resolve <spec> per the documented precedence: a path form (./x, ../x,
    // /abs) beats $DCAP_TEMPLATES/<spec>, which beats a built-in of that name.
    static std::expected<TemplateSource, std::string> resolve(std::string_view spec);

    // Copy the resolved tree into `dest` (which must already exist), verbatim
    // and byte for byte, skipping any .git directory — or write out the
    // embedded files for a built-in.
    std::expected<void, std::string> materialise(const std::filesystem::path& dest) const;

    // "built-in cpp" or the resolved directory path — for the success message.
    std::string describe() const;

private:
    explicit TemplateSource(TemplateRef builtin) : source_(builtin) {}
    explicit TemplateSource(std::filesystem::path dir) : source_(std::move(dir)) {}

    std::variant<TemplateRef, std::filesystem::path> source_;
};

// Names of templates found directly under $DCAP_TEMPLATES, for the usage text.
std::vector<std::string> external_template_names();

} // namespace dcap
