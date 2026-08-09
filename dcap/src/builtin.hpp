#pragma once
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Built-in templates are embedded into the dcap binary with #embed, so they
// work regardless of install location and with no env setup. The registry in
// builtin.cpp is written out by hand — one #embed per file — so adding a file
// to a template means adding an entry there.
namespace dcap {

struct File {
    std::string_view path; // relative to the template root
    std::string_view data;
};

struct Template {
    std::string_view name;
    std::span<const File> files;
};

using TemplateRef = std::reference_wrapper<const Template>;

// Empty if there is no built-in template by that name.
std::optional<TemplateRef> find_builtin(std::string_view name);

// Built-in names for the usage text, e.g. "c | cpp".
std::string builtin_names();

} // namespace dcap
