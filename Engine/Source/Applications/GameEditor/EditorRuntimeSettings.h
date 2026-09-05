#pragma once

namespace ya::editor_runtime_settings
{

void load();
void save();
/// Copy editor.* render keys into runtime.* when the runtime document is empty.
void migrateLegacy();

} // namespace ya::editor_runtime_settings
