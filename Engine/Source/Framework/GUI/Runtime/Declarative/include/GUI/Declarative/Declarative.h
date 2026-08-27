#pragma once

// Static DSL (live construct) and optional screen stack. UIDescription /
// UIReconciler were removed: builders materialize UIElement directly. Future
// adapters may target the same runtime kernel without routing through this DSL.

#include "GUI/Declarative/Build.h"
#include "GUI/Declarative/CompoundBuilder.h"
#include "GUI/Declarative/ScreenStack.h"
