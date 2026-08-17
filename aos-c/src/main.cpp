#include "run.hpp"

/* 讓行程啟動與可重複使用的進入點分開。 */
int main(int argc, char *argv[])
{
    return aos::run(argc, argv);
}
