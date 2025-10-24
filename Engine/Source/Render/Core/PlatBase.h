#pragma once

#include <concepts>

namespace ya
{

template <typename Base>
struct plat_base
{

    template <typename Derived>
    Derived *as()
    {
        static_assert(std::is_base_of_v<Base, Derived>, "T must be derived from plat_base");
        return static_cast<Derived *>(this);
    }
};
} // namespace ya