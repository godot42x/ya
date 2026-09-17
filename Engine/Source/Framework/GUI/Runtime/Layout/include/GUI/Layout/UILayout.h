#pragma once

// Compatibility aggregator for the layout family. Widget/control headers
// should include the typed file they actually use so a canvas change does not
// rebuild box/content hosts:
//   GUI/Layout/UILayoutTypes.h
//   GUI/Layout/UISlot.h
//   GUI/Layout/UILayoutBase.h
//   GUI/Layout/UIBoxLayout.h
//   GUI/Layout/UIOverlayLayout.h
//   GUI/Layout/UIContentLayout.h
//   GUI/Layout/UICanvasLayout.h
//   GUI/Layout/UISplitLayout.h
//   GUI/Layout/UITableLayout.h
//   GUI/Layout/UIScrollLayout.h

#include "GUI/Layout/UILayoutTypes.h"
#include "GUI/Layout/UISlot.h"
#include "GUI/Layout/UILayoutBase.h"
#include "GUI/Layout/UIBoxLayout.h"
#include "GUI/Layout/UIOverlayLayout.h"
#include "GUI/Layout/UIContentLayout.h"
#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Layout/UISplitLayout.h"
#include "GUI/Layout/UITableLayout.h"
#include "GUI/Layout/UIScrollLayout.h"
