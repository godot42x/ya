#include "UUID.h"
#include <random>

namespace ya
{

static std::random_device                      rd;
static std::mt19937                            engine(rd());
static std::uniform_int_distribution<uint64_t> dis;

UUID::UUID()
{
    value = dis(engine);
}

} // namespace ya