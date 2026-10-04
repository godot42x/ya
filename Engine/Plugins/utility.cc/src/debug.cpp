#include "debug.h"

namespace ut
{

log::log(std::source_location loc)
{
    out << "[utility.cc] " << loc.file_name() << ":" << loc.line();
}

log::~log() { out << '\n'; }

} // namespace ut