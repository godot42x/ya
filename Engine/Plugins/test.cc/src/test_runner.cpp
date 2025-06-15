#include "framework.h"

#include <iostream>


int main(int argc, char *argv[])
{
    auto path = "C:/Users/norm/1/craft/Neon/build/windows/x64/debug/test.cc.lib";


    std::cout << "Found " << TestFramework::TestRegistry::GetTestCount() << " tests" << std::endl;

    bool success = TestFramework::TestRegistry::RunAllTests();

    return success ? 0 : 1;
}
