#include <filesystem>
#include <list>

class Name1 {
  std::list<std::string> mounted_dir;
  bool check_if_has_dotaos(std::string dirpath) {
    // if not dir, return false
    // check if the dir has .aos
  }
  void exec() {
    for (auto &dir : mounted_dir) {
    }
  }
};
