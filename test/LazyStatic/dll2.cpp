#include "dll2.h"


void __declspec(dllexport) dll2test()
{
    LazyStatic<TestClass>::init();
    TestClass *c = LazyStatic<TestClass>::get();
    printf("LazyStatic<TestClass>::get() = %p\n", c);
    c->value = 22222;
    printf("LazyStatic<TestClass>::get() = %d\n", c->value);
    printf("DLL2Class::test() called\n");
}