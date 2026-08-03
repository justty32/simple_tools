// dcap <template> <name> — copy a template directory into ./<name>, substitute
// @NAME@ in its CMakeLists.txt, and git init. That is the whole program.
#include "builtin.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const char* kMarker = "CMakeLists.txt"; // what makes a directory a template

void say(const std::string& m) { std::cout << "[dcap] " << m << "\n"; }
void fail(const std::string& m) { std::cerr << "[dcap] error: " << m << "\n"; }

bool exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

bool is_template(const std::string& dir) { return exists(dir + "/" + kMarker); }

bool write_file(const std::string& path, std::string_view content) {
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// $DCAP_TEMPLATES, or "" if unset/empty.
std::string external_root() {
    const char* v = std::getenv("DCAP_TEMPLATES");
    return (v && *v) ? std::string(v) : std::string();
}

void print_usage() {
    std::string extra;
    std::error_code ec;
    if (const std::string root = external_root(); !root.empty())
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory() && is_template(e.path().string()))
                extra += ", " + e.path().filename().string();
    std::cerr << "usage: dcap <template> <name>\n"
              << "  <template>  " << dcap::builtin_names() << " (built-in)"
              << extra << "\n"
              << "              or a path to a template dir (./x, ../x, /abs)\n"
              << "  <name>      new project directory to create\n"
              << "A template is a directory containing a " << kMarker << ".\n"
              << "Set DCAP_TEMPLATES to add named templates; a same-named one "
                 "there wins over a built-in.\n";
}

// Copy a template tree verbatim, byte for byte. Skips any .git directory.
bool copy_tree(const std::string& src, const std::string& dest) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(src, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            return false;
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (!rel.empty() && rel.begin()->string() == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        const std::string out = (fs::path(dest) / rel).string();
        if (it->is_directory())
            fs::create_directories(out, ec);
        else if (it->is_regular_file() && !write_file(out, read_file(it->path().string())))
            return false;
    }
    return true;
}

bool write_builtin(const dcap::Template& t, const std::string& dest) {
    for (std::size_t i = 0; i < t.count; ++i)
        if (!write_file(dest + "/" + std::string(t.files[i].path), t.files[i].data))
            return false;
    return true;
}

// Replace every @NAME@ in the new project's CMakeLists.txt — and nothing else.
void patch_name(const std::string& dir, const std::string& name) {
    const std::string p = dir + "/" + kMarker;
    std::string s = read_file(p);
    const std::string key = "@NAME@";
    for (size_t at = s.find(key); at != std::string::npos;
         at = s.find(key, at + name.size()))
        s.replace(at, key.size(), name);
    write_file(p, s);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }
    const std::string tmpl = argv[1], name = argv[2];
    if (tmpl.empty() || name.empty()) {
        print_usage();
        return 1;
    }

    // Resolve the template: a path (./ or /) beats $DCAP_TEMPLATES/<name>,
    // which beats a built-in of the same name.
    const dcap::Template* builtin = nullptr;
    std::string dir;
    if (tmpl[0] == '.' || tmpl[0] == '/') {
        std::error_code ec;
        const fs::path p = fs::weakly_canonical(fs::absolute(tmpl, ec), ec);
        dir = ec ? tmpl : p.string();
        if (!exists(dir)) {
            fail("template not found: " + tmpl);
            return 1;
        }
        if (!is_template(dir)) {
            fail("not a template (no " + std::string(kMarker) + "): " + dir);
            return 1;
        }
    } else {
        const std::string root = external_root();
        if (!root.empty() && is_template(root + "/" + tmpl))
            dir = root + "/" + tmpl;
        else if ((builtin = dcap::find_builtin(tmpl)) == nullptr) {
            fail("template not found: " + tmpl);
            print_usage();
            return 1;
        }
    }

    if (exists(name)) {
        fail("'" + name + "' already exists");
        return 1;
    }
    std::error_code ec;
    if (!fs::create_directories(name, ec)) {
        fail("cannot create '" + name + "'");
        return 1;
    }

    if (!(builtin ? write_builtin(*builtin, name) : copy_tree(dir, name))) {
        fail("failed to materialize template");
        return 1;
    }
    patch_name(name, name);

    say("created '" + name + "' from " +
        (builtin ? "built-in " + tmpl : dir));
    std::cout.flush();
    if (std::system(("git -C \"" + name + "\" init -q").c_str()) != 0)
        say("warning: git init failed");
    else
        say("git repository initialized");
    say("next: cd " + name + " && cmake -B build && cmake --build build");
    return 0;
}
