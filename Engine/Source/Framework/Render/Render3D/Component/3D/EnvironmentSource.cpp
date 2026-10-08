#include "ECS/Component/3D/EnvironmentSource.h"

namespace ya
{

bool CubemapSource::hasAllFaces() const
{
    for (const auto& file : files) {
        if (file.empty()) {
            return false;
        }
    }
    return true;
}

bool CylindricalSource::hasFile() const
{
    return !filepath.empty();
}

} // namespace ya
