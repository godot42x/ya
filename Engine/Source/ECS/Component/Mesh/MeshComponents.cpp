// Registration anchor for the header-only mesh components. Without a translation unit
// including these headers, the reflection static initializers never run and the component
// types stay unregistered. Keep this file tiny and only include the component headers.
#include "StaticMeshComponent.h"
#include "SkinnedMeshComponent.h"
