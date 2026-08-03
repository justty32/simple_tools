#include "scaffold.hpp"
#include "builtin.hpp"
#include "paths.hpp"
#include "util.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace dcap {

static void say(const std::string& m) { std::cout << "[dcap] " << m << "\n"; }

// A template is any directory that has a Makefile (or makefile).
static bool has_makefile(const std::string& dir) {
    return path_exists(dir + "/Makefile") || path_exists(dir + "/makefile");
}

void print_usage() {
    std::string extra;
    const std::string root = external_root();
    std::error_code ec;
    if (!root.empty())
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory() && has_makefile(e.path().string()))
                extra += ", " + e.path().filename().string();
    std::cerr <<
        "usage: dcap <template> <name>\n"
        "  <template>  c | cpp (built-in)" << extra << "\n"
        "              or a path to a template dir (./x, ../x, /abs)\n"
        "  <name>      new project directory to create\n"
        "A template is a directory containing a Makefile.\n"
        "Set DCAP_TEMPLATES to add named templates (a c/cpp there overrides built-in).\n";
}

// Copy src tree into dest verbatim (byte-for-byte). Skips any .git dir.
static bool copy_dir(const std::string& src, const std::string& dest) {
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
        const fs::path out = fs::path(dest) / rel;
        if (it->is_directory())
            ensure_dir(out.string());
        else if (it->is_regular_file())
            write_file(out.string(), read_file(it->path().string()));
    }
    return true;
}

// Replace @NAME@ with name in the project's Makefile/makefile only.
static void patch_makefile(const std::string& dir, const std::string& name) {
    for (const char* mk : {"/Makefile", "/makefile"}) {
        const std::string p = dir + mk;
        if (path_exists(p))
            write_file(p, substitute_name(read_file(p), name));
    }
}

int scaffold(const std::string& tmpl, const std::string& name) {
    if (tmpl.empty() || name.empty()) {
        print_usage();
        return 1;
    }

    // Resolve template. Order: path (./ or /) > $DCAP_TEMPLATES/<name> > built-in.
    bool builtin = false;
    std::string tdir;
    if (tmpl[0] == '.' || tmpl[0] == '/') {
        tdir = canonical_dir(tmpl);
    } else {
        const std::string root = external_root();
        const std::string cand = root.empty() ? std::string() : root + "/" + tmpl;
        if (!cand.empty() && has_makefile(cand))
            tdir = cand; // a c/cpp here overrides the built-in
        else if (is_builtin(tmpl))
            builtin = true;
        else {
            std::cerr << "[dcap] error: template not found: " << tmpl << "\n";
            print_usage();
            return 1;
        }
    }
    if (!builtin) {
        if (tdir.empty() || !path_exists(tdir)) {
            std::cerr << "[dcap] error: template not found: " << tmpl << "\n";
            print_usage();
            return 1;
        }
        if (!has_makefile(tdir)) {
            std::cerr << "[dcap] error: not a template (no Makefile): " << tdir << "\n";
            return 1;
        }
    }
    if (path_exists(name)) {
        std::cerr << "[dcap] error: '" << name << "' already exists\n";
        return 1;
    }
    if (!ensure_dir(name)) {
        std::cerr << "[dcap] error: cannot create '" << name << "'\n";
        return 1;
    }

    const bool ok = builtin ? write_builtin(tmpl, name) : copy_dir(tdir, name);
    if (!ok) {
        std::cerr << "[dcap] error: failed to materialize template\n";
        return 1;
    }
    patch_makefile(name, name); // only @NAME@ in Makefile/makefile is changed

    say("created '" + name + "' from " + (builtin ? "built-in " + tmpl : tdir));
    if (run("git -C \"" + name + "\" init -q") != 0)
        say("warning: git init failed");
    else
        say("git repository initialized");
    say("next: cd " + name + " && make run");
    return 0;
}

} // namespace dcap
