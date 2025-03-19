

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main()
{
    std::string str = "HelloWorld";

    std::vector<char>     v1(str.begin(), str.end());
    std::vector<uint32_t> v2(str.begin(), str.end());

    printf("v1 size: %zu\n", v1.size());
    printf("v2 size: %zu\n", v2.size());
    printf("%d\n", v1[0]);
    printf("%d\n", v2[0]);

    printf("v2 size: %zu\n", strlen((const char *)v2.data()));

    std::string b(std::string_view(str).substr(3, 3));
    printf("%s\n", b.c_str());


    struct a
    {
    };



    int arr[0];

    printf("%llu\n", sizeof(arr));
    // printf("%d\n", sizeof(void));
    printf("%llu\n", sizeof(a));

    printf("%d\n", sizeof(void));
    printf("%d\n", sizeof(void()));


    return 0;
}
