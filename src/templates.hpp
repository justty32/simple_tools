#pragma once
#include <string>

// Generators for the files that `dcap new` / `dcap new-c` drop into a fresh
// project. Split by language into templates_cpp.cpp and templates_c.cpp.
namespace dcap {

// C++ project (g++ -std=c++20).
std::string cpp_makefile(const std::string& name);
std::string cpp_main();
std::string cpp_gitignore();

// C project (gcc -std=c11).
std::string c_makefile(const std::string& name);
std::string c_main();
std::string c_gitignore();

} // namespace dcap
