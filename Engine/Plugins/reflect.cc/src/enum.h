
#include <array>
#include <iostream>
#include <source_location>
#include <string_view>
#include <unordered_map>


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
    // printf("%s\n", name.data());

    constexpr int    start1 = name.find('=') + 2;
    constexpr size_t start2 = name.rfind("::");

    constexpr int end = name.size() - 1;

    if constexpr (start2 != std::string_view::npos && start2 > start1) {
        // debug(), "branch 2 start2:", start2, "end:", end;
        constexpr std::string_view name2 = name.substr(start2 + 2, end - start2 - 2);
        return name2;
    }
    else {
        // debug(), "branch 1 start1:", start1, "end:", end;
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


// template <typename T>
// constexpr auto enum_size()
// {
//     if constexpr (requires { T::ENUM_MAX; })
//         return static_cast<std::size_t>(T::ENUM_MAX);
//     else if constexpr (requires { T::ENUM_COUNT; })
//         return static_cast<std::size_t>(T::ENUM_COUNT);
//     else {
//         throw std::runtime_error("Enum class does not have ENUM_MAX or ENUM_COUNT member");
//         // static_assert(false, "Enum class does not have ENUM_MAX or ENUM_COUNT member");
//         return 0;
//     }
// }

template <typename T, typename = void, typename = void>
struct enum_size;

template <typename T>
    requires requires { T::ENUM_MAX; }
struct enum_size<T>
{
    static constexpr auto value = static_cast<std::size_t>(T::ENUM_MAX);
};

template <typename T, T LastEnum>
struct enum_size<T, std::integral_constant<T, LastEnum>, void>
{
    static constexpr auto value = static_cast<std::size_t>(LastEnum) + 1;
};


template <typename T, auto Num, std::size_t... Is>
constexpr auto generate_names_array(std::index_sequence<Is...>)
{
    return std::array<std::string_view, Num>{enum_name<static_cast<T>(Is)>()...};
}

template <int UpperBound, typename T>
    requires std::is_enum_v<T>
constexpr auto enum_name(T value)
{
    // constexpr auto num   = enum_size<T>::value;
    constexpr auto names = generate_names_array<T, UpperBound + 1>(std::make_index_sequence<UpperBound + 1>{});
    return names.at(static_cast<std::size_t>(value));
}



}; // namespace detail



template <auto value>
std::string enum_name()
{
    return std::string(detail::enum_name<value>());
}

template <int UpperBound, typename T>
std::string enum_name(T value)
{
    return std::string(detail::enum_name<UpperBound, T>(value));
}


#define GENERATED_ENUM_MISC_WITH_RANGE(ENUM_TYPE_NAME, UPPER_BOUND)                                                                                     \
    inline std::unordered_map<ENUM_TYPE_NAME, std::string> ENUM_TYPE_NAME##2Strings;                                                                    \
    namespace __detail__##ENUM_TYPE_NAME                                                                                                                \
    {                                                                                                                                                   \
        struct Generator                                                                                                                                \
        {                                                                                                                                               \
            Generator()                                                                                                                                 \
            {                                                                                                                                           \
                constexpr int upper_bound = static_cast<int>(ENUM_TYPE_NAME::UPPER_BOUND);                                                              \
                for (int i = 0; i <= upper_bound; i++) {                                                                                                \
                    ENUM_TYPE_NAME##2Strings [static_cast<ENUM_TYPE_NAME>(i)] = enum_name<upper_bound, ENUM_TYPE_NAME>(static_cast<ENUM_TYPE_NAME>(i)); \
                }                                                                                                                                       \
            }                                                                                                                                           \
        };                                                                                                                                              \
        inline static Generator generator;                                                                                                              \
    }

#define GENERATED_ENUM_MISC(ENUM_TYPE_NAME) \
    GENERATED_ENUM_MISC_WITH_RANGE(ENUM_TYPE_NAME, ENUM_MAX)


namespace Test
{
#if !NDEBUG

enum class ETestEnum
{
    Test1 = 0,
    Test2,
    Test3,
    ENUM_MAX,
};
GENERATED_ENUM_MISC(ETestEnum);

// namespace __detail__ETestEnum

inline void a()
{
    ETestEnum2Strings[ETestEnum::Test1] = "Test1";
}
#endif

}; // namespace Test