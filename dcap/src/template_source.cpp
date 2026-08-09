#include "template_source.hpp"
#include "text_file.hpp"
#include <cstdlib>

namespace fs = std::filesystem;

namespace dcap {
namespace {

// $DCAP_TEMPLATES, converted off the C API on the spot.
std::string external_root() {
    const char* v = std::getenv("DCAP_TEMPLATES");
    return (v && *v) ? std::string(v) : std::string();
}

bool is_template(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / kTemplateMarker, ec);
}

// Best-effort absolute + symlink-resolved form, for messages; falls back to
// the input unchanged if the filesystem call fails (e.g. path not present).
fs::path resolved(const fs::path& p) {
    std::error_code ec;
    const fs::path abs = fs::weakly_canonical(fs::absolute(p, ec), ec);
    return ec ? p : abs;
}

std::expected<void, std::string> copy_tree(const fs::path& src, const fs::path& dest) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(src, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            return std::unexpected("cannot read '" + src.string() + "': " + ec.message());
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (!rel.empty() && rel.begin()->string() == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path out = dest / rel;
        if (it->is_directory()) {
            fs::create_directories(out, ec);
            if (ec)
                return std::unexpected("cannot create '" + out.string() + "': " + ec.message());
        } else if (it->is_regular_file()) {
            auto content = read_text(it->path());
            if (!content)
                return std::unexpected(content.error());
            if (auto w = write_text(out, *content); !w)
                return std::unexpected(w.error());
        }
    }
    return {};
}

std::expected<void, std::string> write_builtin(const Template& t, const fs::path& dest) {
    for (const File& f : t.files)
        if (auto w = write_text(dest / f.path, f.data); !w)
            return std::unexpected(w.error());
    return {};
}

} // namespace

std::expected<TemplateSource, std::string> TemplateSource::resolve(std::string_view spec) {
    if (spec.starts_with('.') || spec.starts_with('/')) {
        const fs::path dir = resolved(fs::path(spec));
        std::error_code ec;
        if (!fs::exists(dir, ec))
            return std::unexpected("template not found: " + std::string(spec));
        if (!is_template(dir))
            return std::unexpected("not a template (no " + std::string(kTemplateMarker) +
                                    "): " + dir.string());
        return TemplateSource(dir);
    }

    if (const std::string root = external_root(); !root.empty()) {
        if (const fs::path dir = fs::path(root) / spec; is_template(dir))
            return TemplateSource(resolved(dir));
    }

    if (auto builtin = find_builtin(spec))
        return TemplateSource(*builtin);

    return std::unexpected("template not found: " + std::string(spec));
}

std::expected<void, std::string> TemplateSource::materialise(const fs::path& dest) const {
    if (std::holds_alternative<fs::path>(source_))
        return copy_tree(std::get<fs::path>(source_), dest);
    return write_builtin(std::get<TemplateRef>(source_).get(), dest);
}

std::string TemplateSource::describe() const {
    if (std::holds_alternative<fs::path>(source_))
        return std::get<fs::path>(source_).string();
    return "built-in " + std::string(std::get<TemplateRef>(source_).get().name);
}

std::vector<std::string> external_template_names() {
    std::vector<std::string> names;
    const std::string root = external_root();
    if (root.empty())
        return names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory() && is_template(e.path()))
            names.push_back(e.path().filename().string());
    return names;
}

} // namespace dcap
