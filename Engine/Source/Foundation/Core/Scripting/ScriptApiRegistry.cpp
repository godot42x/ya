#include "ScriptApiRegistry.h"

#include "Foundation/Core/Log.h"

namespace ya
{

ScriptApiRegistry& ScriptApiRegistry::get()
{
    static ScriptApiRegistry registry;
    return registry;
}

void ScriptApiRegistry::registerFunction(std::string name, std::string doc, Json argSchema, Callable callable)
{
    if (_functions.contains(name)) {
        YA_CORE_ERROR("ScriptApiRegistry: duplicate function '{}'", name);
        return;
    }
    FunctionInfo info{
        .name      = name,
        .doc       = std::move(doc),
        .argSchema = std::move(argSchema),
        .callable  = std::move(callable),
    };
    _functions.emplace(std::move(name), std::move(info));
}

bool ScriptApiRegistry::invoke(const std::string& name, const Json& args, Json& outResult, std::string& outError) const
{
    const auto it = _functions.find(name);
    if (it == _functions.end()) {
        outError = "unknown script api: " + name;
        return false;
    }

    try {
        outResult = it->second.callable(args);
        return true;
    }
    catch (const Error& e) {
        outError = e.what();
    }
    catch (const std::exception& e) {
        outError = std::string("script api '") + name + "' failed: " + e.what();
    }
    catch (...) {
        outError = std::string("script api '") + name + "' failed with unknown error";
    }
    return false;
}

ScriptApiRegistry::Json ScriptApiRegistry::buildCommandList() const
{
    Json commands = Json::array();
    for (const auto& [name, info] : _functions) {
        commands.push_back({
            {"name", info.name},
            {"doc", info.doc},
            {"args", info.argSchema},
        });
    }
    return commands;
}

} // namespace ya
