#include "scaffold.hpp"

// dcap <template> <name>
//   argv[1] = template  (c | cpp | <name in $DCAP_TEMPLATES> | path)
//   argv[2] = new project directory name
int main(int argc, char** argv) {
    if (argc < 3) {
        dcap::print_usage();
        return 1;
    }
    return dcap::scaffold(argv[1], argv[2]);
}
