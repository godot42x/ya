#include "ECS/Systems/JSScriptingSystem.h"

#include "Core/Log.h"
#include "Core/Scripting/ScriptApiAsset.h"
#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"

#include <quickjs.h>

#include <format>
#include <vector>

namespace ya
{

namespace
{

using Json = ScriptApiRegistry::Json;

using script::ScriptError;
using script::ScriptRef;
using script::ScriptValue;

// ============================================================================
// Engine objects: one exotic class whose opaque is a ScriptRef. Fields and
// methods are looked up by name on every access through the shared script
// export (Core/Scripting/ScriptBindings.h).
// ============================================================================

JSClassID gObjectClassId = 0;

JSValue throwError(JSContext* ctx, const std::string& message)
{
    return JS_Throw(ctx, JS_NewString(ctx, message.c_str()));
}

const ScriptRef* refOf(JSValueConst value)
{
    return static_cast<const ScriptRef*>(JS_GetOpaque(value, gObjectClassId));
}

JSValue wrapRef(JSContext* ctx, const ScriptRef& ref)
{
    if (!ref) {
        return JS_NULL;
    }
    JSValue object = JS_NewObjectClass(ctx, gObjectClassId);
    JS_SetOpaque(object, new ScriptRef(ref));
    return object;
}

void objectFinalizer(JSRuntime* /*rt*/, JSValue value)
{
    delete static_cast<ScriptRef*>(JS_GetOpaque(value, gObjectClassId));
}

JSValue toJs(JSContext* ctx, const ScriptValue& value)
{
    auto vector = [ctx](const float* components, uint32_t count) {
        JSValue array = JS_NewArray(ctx);
        for (uint32_t i = 0; i < count; ++i) {
            JS_SetPropertyUint32(ctx, array, i, JS_NewFloat64(ctx, components[i]));
        }
        return array;
    };
    return std::visit(
        [&](const auto& v) -> JSValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return JS_NULL;
            }
            else if constexpr (std::is_same_v<T, bool>) {
                return JS_NewBool(ctx, v);
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                return JS_NewInt64(ctx, v);
            }
            else if constexpr (std::is_same_v<T, double>) {
                return JS_NewFloat64(ctx, v);
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                return JS_NewStringLen(ctx, v.data(), v.size());
            }
            else if constexpr (std::is_same_v<T, ScriptRef>) {
                return wrapRef(ctx, v);
            }
            else {
                return vector(&v.x, static_cast<uint32_t>(T::length()));
            }
        },
        value);
}

ScriptValue vectorFromJs(JSContext* ctx, JSValueConst array)
{
    int64_t length = 0;
    if (JS_GetLength(ctx, array, &length) != 0 || length < 2 || length > 4) {
        throw ScriptError("an array passed to the engine must be a vector of 2 to 4 numbers");
    }
    float components[4] = {};
    for (uint32_t i = 0; i < static_cast<uint32_t>(length); ++i) {
        JSValue element = JS_GetPropertyUint32(ctx, array, i);
        double  number  = 0.0;
        const bool bNumber = JS_IsNumber(element) && JS_ToFloat64(ctx, &number, element) == 0;
        JS_FreeValue(ctx, element);
        if (!bNumber) {
            throw ScriptError("an array passed to the engine must be a vector of 2 to 4 numbers");
        }
        components[i] = static_cast<float>(number);
    }
    switch (length) {
    case 2:
        return glm::vec2(components[0], components[1]);
    case 3:
        return glm::vec3(components[0], components[1], components[2]);
    default:
        return glm::vec4(components[0], components[1], components[2], components[3]);
    }
}

ScriptValue fromJs(JSContext* ctx, JSValueConst value)
{
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        return {};
    }
    if (JS_IsBool(value)) {
        return JS_ToBool(ctx, value) != 0;
    }
    if (JS_VALUE_GET_TAG(value) == JS_TAG_INT) {
        return static_cast<int64_t>(JS_VALUE_GET_INT(value));
    }
    if (JS_IsNumber(value)) {
        double number = 0.0;
        JS_ToFloat64(ctx, &number, value);
        return number;
    }
    if (JS_IsString(value)) {
        size_t      length = 0;
        const char* text   = JS_ToCStringLen(ctx, &length, value);
        std::string out(text ? text : "", text ? length : 0);
        JS_FreeCString(ctx, text);
        return out;
    }
    if (const ScriptRef* ref = refOf(value)) {
        return *ref;
    }
    if (JS_IsArray(value)) {
        return vectorFromJs(ctx, value);
    }
    throw ScriptError("this JS value cannot be passed to the engine");
}

std::string atomName(JSContext* ctx, JSAtom atom)
{
    const char* text = JS_AtomToCString(ctx, atom);
    std::string name = text ? text : "";
    JS_FreeCString(ctx, text);
    return name;
}

bool isSymbol(JSContext* ctx, JSAtom atom)
{
    JSValue    value   = JS_AtomToValue(ctx, atom);
    const bool bSymbol = JS_IsSymbol(value);
    JS_FreeValue(ctx, value);
    return bSymbol;
}

JSValue methodCall(JSContext* ctx, JSValueConst thisValue, int argc, JSValueConst* argv, int /*magic*/, JSValueConst* data)
{
    const ScriptRef* self = refOf(thisValue);
    size_t           length = 0;
    const char*      text   = JS_ToCStringLen(ctx, &length, data[0]);
    const std::string name(text ? text : "", text ? length : 0);
    JS_FreeCString(ctx, text);
    if (!self) {
        return throwError(ctx, std::format("{}: not called on an engine object", name));
    }
    try {
        std::vector<ScriptValue> args;
        args.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.push_back(fromJs(ctx, argv[i]));
        }
        return toJs(ctx, script::callMethod(*self, name, args));
    }
    catch (const std::exception& e) {
        return throwError(ctx, std::format("{}: {}", name, e.what()));
    }
}

JSValue objectGetProperty(JSContext* ctx, JSValueConst object, JSAtom atom, JSValueConst /*receiver*/)
{
    const ScriptRef* self = refOf(object);
    if (self && !isSymbol(ctx, atom)) {
        const std::string name = atomName(ctx, atom);
        try {
            if (const script::ScriptField* field = script::findField(self->type, name)) {
                return toJs(ctx, script::readField(*self, *field));
            }
            if (script::findMethod(self->type, name)) {
                JSValue nameValue = JS_NewStringLen(ctx, name.data(), name.size());
                JSValue function  = JS_NewCFunctionData(ctx, &methodCall, 0, 0, 1, &nameValue);
                JS_FreeValue(ctx, nameValue);
                return function;
            }
        }
        catch (const std::exception& e) {
            return throwError(ctx, std::format("{}: {}", name, e.what()));
        }
    }
    // Not an engine member: plain object behavior (toString, toJSON lookups...).
    JSValue prototype = JS_GetPrototype(ctx, object);
    JSValue value     = JS_IsObject(prototype) ? JS_GetProperty(ctx, prototype, atom) : JS_UNDEFINED;
    JS_FreeValue(ctx, prototype);
    return value;
}

int objectSetProperty(JSContext* ctx, JSValueConst object, JSAtom atom, JSValueConst value, JSValueConst /*receiver*/, int /*flags*/)
{
    const ScriptRef* self = refOf(object);
    const std::string name = atomName(ctx, atom);
    try {
        if (!self) {
            throw ScriptError("not an engine object");
        }
        script::writeField(*self, name, fromJs(ctx, value));
        return 1;
    }
    catch (const std::exception& e) {
        throwError(ctx, std::format("{}: {}", name, e.what()));
        return -1;
    }
}

JSClassExoticMethods gObjectExotic{
    .get_property = &objectGetProperty,
    .set_property = &objectSetProperty,
};

// ============================================================================
// JSON <-> JS (registry commands and eval results)
// ============================================================================

const char* jsToJsonString(JSContext* ctx, JSValueConst value)
{
    JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        JS_FreeValue(ctx, json);
        return nullptr;
    }
    const char* text = JS_ToCString(ctx, json);
    JS_FreeValue(ctx, json);
    return text;
}

Json jsonFromJsValue(JSContext* ctx, JSValueConst value)
{
    const char* text = jsToJsonString(ctx, value);
    if (text == nullptr) {
        return Json(nullptr);
    }
    const std::string textStr(text);
    JS_FreeCString(ctx, text);
    return Json::parse(textStr);
}

JSValue jsonToJs(JSContext* ctx, const Json& json)
{
    const std::string text  = json.dump();
    JSValue           value = JS_ParseJSON(ctx, text.c_str(), text.size(), "<api>");
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return JS_UNDEFINED;
    }
    return value;
}

// ============================================================================
// ya.scene / ya.entity
// ============================================================================

JSValue sceneActiveFunction(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/, JSValueConst* /*argv*/)
{
    return wrapRef(ctx, script::sceneRef(ScriptApiRegistry::get().getActiveScene()));
}

JSValue entityCreateFunction(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv)
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        return throwError(ctx, "entity.create: no active scene");
    }
    const char*       nameCStr = argc >= 1 ? JS_ToCString(ctx, argv[0]) : nullptr;
    const std::string name     = nameCStr != nullptr ? nameCStr : "Entity";
    if (nameCStr != nullptr) {
        JS_FreeCString(ctx, nameCStr);
    }
    Node3D* const node = scene->createNode3D(name);
    if (node == nullptr || node->getEntity() == nullptr) {
        return throwError(ctx, "entity.create: failed to create entity");
    }
    return wrapRef(ctx, script::entityRef(node->getEntity()));
}

JSValue entityGetFunction(JSContext* ctx, JSValueConst /*this_val*/, int argc, JSValueConst* argv)
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        return throwError(ctx, "entity.get: no active scene");
    }
    uint32_t id = 0;
    if (argc >= 1 && JS_ToUint32(ctx, &id, argv[0])) {
        return throwError(ctx, "entity.get: invalid id");
    }
    Entity* entity = scene->getEntityByEnttID(entt::entity{id});
    if (entity == nullptr) {
        return throwError(ctx, std::format("entity.get: entity {} not found", id));
    }
    return wrapRef(ctx, script::entityRef(entity));
}

JSValue entityListFunction(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/, JSValueConst* /*argv*/)
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        return throwError(ctx, "entity.list: no active scene");
    }
    JSValue  array = JS_NewArray(ctx);
    uint32_t index = 0;
    for (auto& [handle, entity] : scene->_entityMap) {
        (void)handle;
        JS_SetPropertyUint32(ctx, array, index++, wrapRef(ctx, script::entityRef(&entity)));
    }
    return array;
}

// ============================================================================
// Registry -> JS namespace export ("function library" objects)
// ============================================================================
//
// Every ScriptApiRegistry command "ns.fn" becomes `ya.ns.fn(...)` so libraries
// like EditAssetLibrary surface as plain JS objects. Hand-written module
// functions (ya.entity.create / ya.scene.active) take precedence - the
// registry entry for the same name is skipped, never overwritten.
//
// Argument convention (derived from the command's argSchema):
//   - no schema keys            -> fn()
//   - exactly one schema key    -> fn(value)   positional
//   - multiple schema keys      -> fn({key: value, ...}) params object

struct LibraryFunctionBinding
{
    std::string              name;
    std::vector<std::string> argNames;
};

void libraryFunctionFinalizer(void* opaque)
{
    delete static_cast<LibraryFunctionBinding*>(opaque);
}

JSValue libraryFunctionClosure(JSContext* ctx,
                               JSValueConst /*this_val*/,
                               int argc,
                               JSValueConst* argv,
                               int /*magic*/,
                               void* opaque)
{
    const auto* binding = static_cast<const LibraryFunctionBinding*>(opaque);

    Json args = Json::object();
    if (binding->argNames.size() == 1) {
        args[binding->argNames[0]] = argc >= 1 ? jsonFromJsValue(ctx, argv[0]) : nullptr;
    }
    else if (binding->argNames.size() > 1) {
        if (argc == 0) {
            // Leave the defaults to the callable.
        }
        else if (argc == 1 && JS_IsObject(argv[0])) {
            args = jsonFromJsValue(ctx, argv[0]);
        }
        else {
            return throwError(ctx,
                              "command '" + binding->name +
                                  "' takes multiple params; pass a single object {key: value, ...}");
        }
    }

    Json        result;
    std::string error;
    if (!ScriptApiRegistry::get().invoke(binding->name, args, result, error)) {
        return throwError(ctx, error);
    }
    return jsonToJs(ctx, result);
}

/// Creates `ya.<namespace>.<fn>` objects for every registered command.
void buildRegistryLibraryObjects(JSContext* ctx, JSValue yaGlobal)
{
    for (const auto& [name, info] : ScriptApiRegistry::get().functions()) {
        std::vector<std::string> parts;
        size_t                   start = 0;
        while (start <= name.size()) {
            const size_t dot = name.find('.', start);
            parts.push_back(name.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
            if (dot == std::string::npos) {
                break;
            }
            start = dot + 1;
        }

        // Walk/create the namespace chain.
        JSValue current = JS_DupValue(ctx, yaGlobal);
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            JSValue child = JS_GetPropertyStr(ctx, current, parts[i].c_str());
            if (JS_IsUndefined(child)) {
                JS_FreeValue(ctx, child);
                child = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, current, parts[i].c_str(), child); // steals ref
                child = JS_GetPropertyStr(ctx, current, parts[i].c_str());
            }
            JS_FreeValue(ctx, current);
            current = child;
        }

        // Leaf: skip names already provided by hand-written module functions.
        const std::string& leaf      = parts.back();
        JSValue            existing  = JS_GetPropertyStr(ctx, current, leaf.c_str());
        const bool         bShadowed = !JS_IsUndefined(existing);
        JS_FreeValue(ctx, existing);
        if (!bShadowed) {
            std::vector<std::string> argNames;
            for (auto it = info.argSchema.begin(); it != info.argSchema.end(); ++it) {
                argNames.push_back(it.key());
            }
            JSValue fn = JS_NewCClosure(ctx,
                                        libraryFunctionClosure,
                                        name.c_str(),
                                        libraryFunctionFinalizer,
                                        0,
                                        0,
                                        new LibraryFunctionBinding{name, std::move(argNames)});
            JS_SetPropertyStr(ctx, current, leaf.c_str(), fn);
        }
        JS_FreeValue(ctx, current);
    }
}

} // namespace

struct JSScriptingSystem::Impl
{
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;

    ~Impl()
    {
        if (context != nullptr) {
            JS_FreeContext(context);
            context = nullptr;
        }
        if (runtime != nullptr) {
            JS_FreeRuntime(runtime);
            runtime = nullptr;
        }
    }
};

// Out-of-line so TUs that only see the pimpl declaration never instantiate
// unique_ptr<Impl> cleanup (Impl is incomplete outside this TU).
JSScriptingSystem::JSScriptingSystem()  = default;
JSScriptingSystem::~JSScriptingSystem() = default;

void JSScriptingSystem::init()
{
    script::ensureSceneScriptBindings();

    _impl = std::make_unique<Impl>();

    _impl->runtime = JS_NewRuntime();
    _impl->context = JS_NewContext(_impl->runtime);
    if (_impl->context == nullptr) {
        YA_CORE_ERROR("JSScriptingSystem: failed to create quickjs context");
        return;
    }

    JS_NewClassID(_impl->runtime, &gObjectClassId);
    const JSClassDef objectClassDef{
        .class_name = "EngineObject",
        .finalizer  = objectFinalizer,
        .exotic     = &gObjectExotic,
    };
    JS_NewClass(_impl->runtime, gObjectClassId, &objectClassDef);

    // Global surface: ya.scene.active(), ya.entity.create/get/list, ya.__commands
    // Ownership of `ya` transfers to the global object via JS_SetPropertyStr.
    JSValue yaGlobal = JS_NewObject(_impl->context);
    JSValue global   = JS_GetGlobalObject(_impl->context);
    JS_SetPropertyStr(_impl->context, global, "ya", yaGlobal);
    JS_FreeValue(_impl->context, global);

    JSValue sceneModule = JS_NewObject(_impl->context);
    JS_SetPropertyStr(_impl->context, sceneModule, "active", JS_NewCFunction(_impl->context, sceneActiveFunction, "active", 0));
    JS_SetPropertyStr(_impl->context, yaGlobal, "scene", sceneModule);

    JSValue entityModule = JS_NewObject(_impl->context);
    JS_SetPropertyStr(_impl->context, entityModule, "create", JS_NewCFunction(_impl->context, entityCreateFunction, "create", 1));
    JS_SetPropertyStr(_impl->context, entityModule, "get", JS_NewCFunction(_impl->context, entityGetFunction, "get", 1));
    JS_SetPropertyStr(_impl->context, entityModule, "list", JS_NewCFunction(_impl->context, entityListFunction, "list", 0));
    JS_SetPropertyStr(_impl->context, yaGlobal, "entity", entityModule);

    // Function libraries: every registered command becomes ya.<ns>.<fn>.
    buildRegistryLibraryObjects(_impl->context, yaGlobal);

    JS_SetPropertyStr(_impl->context, yaGlobal, "__commands", jsonToJs(_impl->context, ScriptApiRegistry::get().buildCommandList()));
}

void JSScriptingSystem::shutdown()
{
    _impl.reset();
}

JSScriptingSystem::EvalResult JSScriptingSystem::evalJS(const std::string& source, const std::string& filename)
{
    EvalResult result;
    if (_impl == nullptr || _impl->context == nullptr) {
        result.error = "js scripting unavailable";
        return result;
    }

    JSValue value = JS_Eval(_impl->context, source.c_str(), source.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) {
        JSValue     exception = JS_GetException(_impl->context);
        const char* message   = JS_ToCString(_impl->context, exception);
        result.error          = message != nullptr ? message : "unknown js error";
        if (message != nullptr) {
            JS_FreeCString(_impl->context, message);
        }
        JS_FreeValue(_impl->context, exception);
        JS_FreeValue(_impl->context, value);
        return result;
    }

    if (JS_IsUndefined(value)) {
        result.value = nullptr;
    }
    else {
        const char* json = jsToJsonString(_impl->context, value);
        if (json == nullptr) {
            result.error = "failed to serialize js result";
        }
        else {
            const std::string jsonText(json);
            JS_FreeCString(_impl->context, json);
            try {
                result.value = Json::parse(jsonText);
            }
            catch (const std::exception& e) {
                result.error = std::string("failed to parse js result: ") + e.what();
            }
        }
    }
    JS_FreeValue(_impl->context, value);

    result.ok = result.error.empty();
    return result;
}

bool JSScriptingSystem::invoke(const std::string&             name,
                               const ScriptApiRegistry::Json& args,
                               ScriptApiRegistry::Json&       outResult,
                               std::string&                   outError)
{
    return ScriptApiRegistry::get().invoke(name, args, outResult, outError);
}

} // namespace ya
