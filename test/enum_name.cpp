
#include <array>
#include <iostream>
#include <source_location>



#include <string_view>


struct debug
{
    std::ostream &out = std::cout;

    debug(std::string_view sig = "-", std::source_location location = std::source_location::current())
    {
#if NDEBUG
        out << sig << ' ';
#else
        out << sig << ' ' << location.file_name() << ':' << location.line() << ' ';
#endif
    }

    debug &operator,(auto msg)
    {
        out << msg << ' ';
        return *this;
    }

    ~debug()
    {
        out << '\n';
    }
};



enum NormalEnum
{
    NormalEnumValue1 = 1,
    NormalEnumValue2 = 2,
    NormalEnumValue3 = 3,
    ENUM_MAX,
};

enum class ClassEnum
{
    ClassEnumValue1 = 1,
    ClassEnumValue2 = 2,
    ClassEnumValue3 = 3,
    ENUM_Count,
};


template <auto T>
void print_fn()
{
#if __GNUC__ || __clang__
    std::cout << __PRETTY_FUNCTION__ << std::endl;
#elif _MSC_VER
    std::cout << __FUNCSIG__ << std::endl;
#endif
}


void test1()
{
    print_fn<1>();
    print_fn<NormalEnumValue1>();
    print_fn<ClassEnum::ClassEnumValue1>();

    // on windows
    // void print_fn() [with auto T = 1]
    // void print_fn() [with auto T = NormalEnumValue1]
    // void print_fn() [with auto T = ClassEnum::ClassEnumValue1]
}


namespace detail
{

template <auto value>
constexpr auto enum_name()
{

// on gcc
// void print_fn() [with auto T = 1]
// void print_fn() [with auto T = NormalEnumValue1]
// void print_fn() [with auto T = ClassEnum::ClassEnumValue1]
#if __GNUC__ || __clang__
    constexpr std::string_view name = __PRETTY_FUNCTION__;
    printf("%s\n", name.data());

    constexpr int    start1 = name.find('=') + 2;
    constexpr size_t start2 = name.rfind("::");

    constexpr int end = name.size() - 1;

    if constexpr (start2 != std::string_view::npos && start2 > start1) {
        debug(), "branch 2 start2:", start2, "end:", end;
        constexpr std::string_view name2 = name.substr(start2 + 2, end - start2 - 2);
        return name2;
    }
    else {
        debug(), "branch 1 start1:", start1, "end:", end;
        constexpr std::string_view name1 = name.substr(start1, end - start1);
        return name1;
    };
#elif _MSC_VER
    constexpr std::string_view name = __FUNCSIG__;
    // printf("%s\n", name.data());
    constexpr std::size_t start1 = name.find('<') + 1;
    constexpr int         start2 = name.rfind("::");
    constexpr std::size_t end    = name.rfind(">(");

    // auto __cdecl detail::enum_name<ClassEnum::ClassEnumValue1>(void)
    // auto __cdecl detail::enum_name<NormalEnumValue1>(void)
    if constexpr (start2 != std::string_view::npos && start2 > start1) {
        // debug(), "branch 2 start2:", start2, "end:", end;
        constexpr std::string_view name2 = name.substr(start2 + 2, end - start2 - 2);
        return name2;
    }
    else {
        // printf("branch 1 start1: %zu end: %zu\n", start1, end);
        // debug(), "branch 1 start1:", start1, "end:", end;
        constexpr std::string_view name1 = name.substr(start1, end - start1);
        return name1;
    };
#endif
}


template <typename T>
constexpr auto enum_size()
{
    if constexpr (requires { T::ENUM_COUNT; })
        return static_cast<std::size_t>(T::ENUM_COUNT);
    else if constexpr (requires { T::ENUM_MAX; })
        return static_cast<std::size_t>(T::ENUM_MAX);
    else
        return 0;
}

template <typename T>
    requires std::is_enum_v<T>
constexpr auto enum_name(T value)
{
    constexpr auto num       = enum_size<T>();
    constexpr auto gen_names = []<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::array<std::string_view, num>{
            enum_name<static_cast<T>(Is)>()...};
    };
    constexpr auto names = gen_names(std::make_index_sequence<num>{});

    return names[static_cast<std::size_t>(value)];
}



}; // namespace detail



template <auto value>
std::string enum_name()
{
    return std::string(detail::enum_name<value>());
}

template <typename T>
std::string enum_name(T value)
{
    return std::string(detail::enum_name(value));
}

void test2()
{
    std::string a = std::string{enum_name<NormalEnumValue1>()};
    printf("%s\n", a.data());

    auto b = enum_name<ClassEnum::ClassEnumValue1>();
    printf("%s\n", b.data());

    ClassEnum e = ClassEnum::ClassEnumValue1;
    auto      c = enum_name(e);
    printf("%s\n", c.data());
}


int main()
{
    try {
        test1();
        printf("====================\n");
        test2();
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
    }
    catch (...) {
        std::cerr << "Unknown error\n";
    }

    return 0;
}