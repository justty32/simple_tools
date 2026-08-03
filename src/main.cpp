#include "commands.hpp"
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace dcap;
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage();
        return 0;
    }

    const std::string& cmd = args[0];
    const std::string arg1 = args.size() > 1 ? args[1] : std::string();

    if (cmd == "new")
        return cmd_new(arg1, Lang::Cpp);
    if (cmd == "new-c")
        return cmd_new(arg1, Lang::C);
    if (cmd == "build")
        return cmd_build();
    if (cmd == "help" || cmd == "-h" || cmd == "--help") {
        print_usage();
        return 0;
    }

    std::cerr << "[dcap] unknown command: " << cmd << "\n\n";
    print_usage();
    return 1;
}
