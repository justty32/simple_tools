#include "commands.hpp"
#include "templates.hpp"
#include "util.hpp"
#include "platform.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace dcap {

static void say(const std::string& msg) {
    std::cout << "[dcap] " << msg << "\n";
}

int cmd_new(const std::string& name, Lang lang) {
    if (name.empty()) {
        std::cerr << "[dcap] error: missing project name\n";
        return 1;
    }
    if (path_exists(name)) {
        std::cerr << "[dcap] error: '" << name << "' already exists\n";
        return 1;
    }
    if (!ensure_dir(name)) {
        std::cerr << "[dcap] error: cannot create directory '" << name << "'\n";
        return 1;
    }

    const bool cpp = (lang == Lang::Cpp);
    const std::string makefile = cpp ? cpp_makefile(name) : c_makefile(name);
    const std::string mainSrc  = cpp ? cpp_main()         : c_main();
    const std::string gitign   = cpp ? cpp_gitignore()    : c_gitignore();
    const std::string mainName = cpp ? "main.cpp" : "main.c";

    write_file(name + "/Makefile", makefile);
    write_file(name + "/" + mainName, mainSrc);
    write_file(name + "/.gitignore", gitign);
    say("created " + std::string(cpp ? "C++" : "C") + " project '" + name + "'");

    if (run("git -C \"" + name + "\" init -q") != 0)
        say("warning: git init failed (is git installed?)");
    else
        say("git repository initialized");

    say("next: cd " + name + " && dcap build");
    return 0;
}

// Locate the executable produced under bin/. Prefers a .exe on Windows and an
// extensionless file on UNIX.
static std::string find_binary() {
    fs::path bin = "bin";
    std::error_code ec;
    if (!fs::exists(bin, ec))
        return "";
    for (const auto& entry : fs::directory_iterator(bin, ec)) {
        if (!entry.is_regular_file())
            continue;
        const fs::path& p = entry.path();
#ifdef _WIN32
        if (p.extension() == ".exe")
            return p.string();
#else
        if (p.extension().empty())
            return p.string();
#endif
    }
    return "";
}

int cmd_build() {
    if (!path_exists("Makefile")) {
        std::cerr << "[dcap] error: no Makefile in current directory\n";
        return 1;
    }

    const std::string make = make_program();
    say("building with " + make + " -j4 ...");
    const int rc = run(make + " -j4");
    if (rc != 0) {
        std::cerr << "[dcap] build failed (exit " << rc << ")\n";
        return rc ? rc : 1;
    }
    say("build ok");

    const std::string bin = find_binary();
    if (bin.empty()) {
        say("no binary found in bin/ to run");
        return 0;
    }
    say("running " + bin + " ...");
    std::cout << "----------------------------------------\n";
#ifdef _WIN32
    const int r = run("\"" + bin + "\"");
#else
    const int r = run("./" + bin);
#endif
    std::cout << "----------------------------------------\n";
    say("program exited with code " + std::to_string(r));
    return 0;
}

void print_usage() {
    std::cout <<
        "dcap - lightweight C/C++ scaffolding & build tool\n\n"
        "Usage:\n"
        "  dcap new <name>     create a C++ project\n"
        "  dcap new-c <name>   create a C project\n"
        "  dcap build          build current project and run its binary\n"
        "  dcap help           show this help\n";
}

} // namespace dcap
