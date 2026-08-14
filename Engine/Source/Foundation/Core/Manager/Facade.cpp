#include "Facade.h"

namespace ya
{
namespace
{
FacadeMode s_facade;
} // namespace

YA_CORE_API FacadeMode& facade()
{
    return s_facade;
}

} // namespace ya
