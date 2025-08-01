

#include "class.h"
#include "dll2.h"
#include "lazystatic.h"
#include <cstdio>


void __declspec(dllimport) dll2test();

int main()
{
    LazyStatic<TestClass>::init();
    TestClass *c = LazyStatic<TestClass>::get();
    printf("LazyStatic<TestClass>::get() = %p, %d\n", c, c->value);
    c->value = 10;

    printf("===================\n");
    dll2test();


    printf("===================\n");
    printf("LazyStatic<TestClass>::get() = %d\n", c->value);

    return 0;
}