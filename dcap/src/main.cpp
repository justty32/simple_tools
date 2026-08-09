// dcap <template> <name> — copy a template directory into ./<name>, substitute
// @NAME@ in its CMakeLists.txt, and git init. That is the whole program.
#include "builtin.hpp"
#include "scaffold.hpp"
#include "template_source.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

void say(const std::string& m) { std::cout << "[dcap] " << m << "\n"; }
void fail(const std::string& m) { std::cerr << "[dcap] error: " << m << "\n"; }

bool exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

void print_usage() {
    std::string extra;
    for (const std::string& n : dcap::external_template_names())
        extra += ", " + n;
    std::cerr << "usage: dcap <template> <name>\n"
              << "  <template>  " << dcap::builtin_names() << " (built-in)"
              << extra << "\n"
              << "              or a path to a template dir (./x, ../x, /abs)\n"
              << "  <name>      new project directory to create\n"
              << "A template is a directory containing a " << dcap::kTemplateMarker << ".\n"
              << "Set DCAP_TEMPLATES to add named templates; a same-named one "
                 "there wins over a built-in.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { // guards the range below as well as the arity
        print_usage();
        return 1;
    }
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args[0].empty() || args[1].empty()) {
        print_usage();
        return 1;
    }
    const std::string tmpl(args[0]);
    const std::string name(args[1]);

    auto source = dcap::TemplateSource::resolve(tmpl);
    if (!source) {
        fail(source.error());
        // Matches the old behaviour: only a bare (non-path) template name
        // that could not be found also gets the usage text.
        if (!tmpl.starts_with('.') && !tmpl.starts_with('/'))
            print_usage();
        return 1;
    }

    if (exists(name)) {
        fail("'" + name + "' already exists");
        return 1;
    }

    dcap::NewDirectory dir(name);
    if (!dir.ok()) {
        fail("cannot create '" + name + "'");
        return 1;
    }

    if (auto r = source->materialise(name); !r) {
        fail(r.error());
        return 1; // dir's destructor removes the half-made directory
    }
    if (auto r = dcap::patch_name(name, name); !r) {
        fail(r.error());
        return 1;
    }
    dir.commit();

    say("created '" + name + "' from " + source->describe());
    std::cout.flush();
    if (auto r = dcap::git_init(name); !r)
        say("warning: git init failed");
    else
        say("git repository initialized");
    say("next: cd " + name +
        " && cmake -B build && cmake --build build && ./bin/" + name);
    return 0;
}
