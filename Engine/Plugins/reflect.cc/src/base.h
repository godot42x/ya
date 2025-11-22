#pragma once

#include <string>


struct A
{
    int  b;
    void c() {}
};


struct Field
{
    std::string name;

    enum Type
    {
        variable,
        function,
    } ty;
};


template <typename Ret, typename... Args>
struct MemberFunc : public Field
{
    using FuncPtr = Ret (*)(Args...);
};

struct RClass
{
};

struct Registry
{
};

void a()
{
    auto a = decltype(A::b){};
    auto f = decltype(&A::c){};
}