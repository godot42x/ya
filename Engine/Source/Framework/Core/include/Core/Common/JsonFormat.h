#pragma once

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <string>

namespace ya
{

// Pretty-print `value` with `indent` spaces per level. Objects always expand,
// one key per line, in nlohmann's key order. An array whose elements are all
// scalars stays on one line, and wraps between elements once the line would
// pass `wrapColumn` (counted in bytes). Arrays that contain an object or
// another array expand. Scalar text is `json(scalar).dump()` (`ensure_ascii`
// stays false).
[[nodiscard]] YA_CORE_API std::string dumpJsonCompactLeaves(const nlohmann::json& value,
                                                            int                    indent     = 2,
                                                            int                    wrapColumn = 100);

} // namespace ya
