#pragma once

// Compatibility bridge (G4.1): Reactive moved to Runtime/Binding. Keep the
// legacy widget include path alive while callers migrate to
// `GUI/Binding/Reactive.h`.

#include "../Binding/Reactive.h"
