

#include "utility.cc/string_utils.h"



int main()
{
    const char *a = "        bc     ";

    auto ret = ut::str::trim(a);
    printf("%s\n", ret.data());
}