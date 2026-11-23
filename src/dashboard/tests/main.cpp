#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

std::string g_shotDir;

int main(int argc, char **argv)
{
    // llvmpipe on every host: the tests need no GPU and never touch one
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    ::testing::InitGoogleTest(&argc, argv);
    for(int i=1; i<argc; ++i)
    {
        std::string arg=argv[i];
        if(arg=="--shots" && i+1<argc)
            g_shotDir=argv[++i];
    }
    return RUN_ALL_TESTS();
}
