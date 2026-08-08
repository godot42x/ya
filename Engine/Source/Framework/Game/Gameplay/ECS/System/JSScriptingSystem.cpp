#include "JSScriptingSystem.h"

#include "Foundation/Core/Log.h"
#include "Framework/Game/Gameplay/ECS/ECSRegistry.h"
#include "Foundation/Core/Reflection/InstanceRef.h"
#include "Foundation/Core/Reflection/MethodReflection.h"
#include "Foundation/Core/Reflection/ReflectionSerializer.h"
#include "Foundation/Core/Scripting/ScriptApiAsset.h"
#include "Framework/Game/Gameplay/ECS/Component.h"
#include "Framework/Game/Gameplay/ECS/Entity.h"
#include "Framework/Game/Render/Render3D/Scene.h"

#include <quickjs.h>

#include <format>
#include <unordered_map>
#include <vector>

namespace ya
{

namespace
{

using Json = ScriptApiRegistry::Json;

// ============================================================================
// Script handle: every wrapped JS object carries (typeIndex, ptr).
// ============================================================================

struct ScriptHandle
{
    ya::type_index_t typeIndex = 0;
    void*            ptr       = nullptr;
};

JSClassID gWrapperClassId = 0;

void wrapperFinalizer(JSRuntime* /*rt*/, JSValue val)
{
    auto* handle = static_cast<ScriptHandle*>(JS_GetOpaque(val, gWrapperClassId));
    delete handle;
}

ScriptHandle* getHandle(JSContext* ctx, JSValueConst obj)
{
    return static_cast<ScriptHandle*>(JS_GetOpaque2(ctx, obj, gWrapperClassId));
}

JSValue throwError(JSContext* ctx, const std::string& message)
{
    return JS_Throw(ctx, JS_NewString(ctx, message.c_str()));
}

/// Defined below with the instance-wrapping helpers.
JSValue jsonToJsWithHandles(JSContext* ctx, const Json& json);

// ============================================================================
// JSON <-> JS conversion (via quickjs JSON stringify / parse)
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

Json jsonArrayFromJsArgs(JSContext* ctx, int argc, JSValueConst* argv)
{
    JSValue array = JS_NewArray(ctx);
    for (int i = 0; i < argc; ++i) {
        JS_SetPropertyUint32(ctx, array, static_cast<uint32_t>(i), JS_DupValue(ctx, argv[i]));
    }
    const char* text = jsToJsonString(ctx, array);
    JS_FreeValue(ctx, array);
    if (text == nullptr) {
        return Json::array();
    }
    const std::string textStr(text);
    JS_FreeCString(ctx, text);
    return Json::parse(textStr);
}

JSValue jsonToJs(JSContext* ctx, const Json& json)
{
    const std::string text = json.dump();
    JSValue value = JS_ParseJSON(ctx, text.c_str(), text.size(), "<api>");
    if (JS_IsException(value)) {
        JS_FreeValue(ctx, value);
        return JS_UNDEFINED;
    }
    return value;
}

// ============================================================================
// Component class binding (fully reflection-driven)
// ============================================================================

struct FieldBinding
{
    ya::type_index_t typeIndex = 0;
    std::string      fieldName;
};

void fieldBindingFinalizer(void* opaque)
{
    delete static_cast<FieldBinding*>(opaque);
}

struct MethodBinding
{
    ya::type_index_t typeIndex = 0;
    std::string      methodName;
};

void methodBindingFinalizer(void* opaque)
{
    delete static_cast<MethodBinding*>(opaque);
}

JSValue fieldGetterClosure(JSContext* ctx,
                           JSValueConst this_val,
                           int /*argc*/,
                           JSValueConst* /*argv*/,
                           int /*magic*/,
                           void* opaque)
{
    const auto* binding = static_cast<const FieldBinding*>(opaque);
    auto*       handle  = getHandle(ctx, this_val);
    if (handle == nullptr || handle->typeIndex != binding->typeIndex) {
        return throwError(ctx, "field get: invalid component instance");
    }

    const auto* cls = ClassRegistry::instance().getClass(binding->typeIndex);
    const auto* prop = cls != nullptr ? cls->getProperty(binding->fieldName) : nullptr;
    if (prop == nullptr) {
        return throwError(ctx, "field get: unknown property '" + binding->fieldName + "'");
    }
    return jsonToJs(ctx, ReflectionSerializer::serializeProperty(handle->ptr, *prop));
}

JSValue fieldSetterClosure(JSContext* ctx,
                           JSValueConst this_val,
                           int /*argc*/,
                           JSValueConst* argv,
                           int /*magic*/,
                           void* opaque)
{
    const auto* binding = static_cast<const FieldBinding*>(opaque);
    auto*       handle  = getHandle(ctx, this_val);
    if (handle == nullptr || handle->typeIndex != binding->typeIndex) {
        return throwError(ctx, "field set: invalid component instance");
    }

    const auto* cls  = ClassRegistry::instance().getClass(binding->typeIndex);
    const auto* prop = cls != nullptr ? cls->getProperty(binding->fieldName) : nullptr;
    if (prop == nullptr) {
        return throwError(ctx, "field set: unknown property '" + binding->fieldName + "'");
    }

    try {
        ReflectionSerializer::deserializeProperty(*prop, handle->ptr, jsonFromJsValue(ctx, argv[0]));
        // Only component instances carry the onPostSerialize hook.
        if (ECSRegistry::get().getComponentOps(handle->typeIndex) != nullptr) {
            static_cast<IComponent*>(handle->ptr)->onPostSerialize();
        }
    }
    catch (const std::exception& e) {
        return throwError(ctx, std::string("field set '") + binding->fieldName + "' failed: " + e.what());
    }
    return JS_UNDEFINED;
}

JSValue reflectedMethodTrampoline(JSContext* ctx,
                                  JSValueConst this_val,
                                  int argc,
                                  JSValueConst* argv,
                                  int /*magic*/,
                                  void* opaque)
{
    const auto* binding = static_cast<const MethodBinding*>(opaque);

    auto* handle = getHandle(ctx, this_val);
    if (handle == nullptr || handle->typeIndex != binding->typeIndex) {
        return throwError(ctx, "method call: invalid instance");
    }

    const auto* cls = ClassRegistry::instance().getClass(handle->typeIndex);
    if (cls == nullptr) {
        return throwError(ctx, "method call: component class is not registered");
    }
    const auto it = cls->functions.find(binding->methodName);
    const auto jsonInvoker = it != cls->functions.end()
                                 ? ::ya::reflection::detail::findJsonInvoker(it->second)
                                 : ::ya::reflection::detail::JsonInvoker{};
    if (!jsonInvoker) {
        return throwError(ctx, "method call: unknown method '" + binding->methodName + "'");
    }

    try {
        return jsonToJsWithHandles(ctx, jsonInvoker(handle->ptr, jsonArrayFromJsArgs(ctx, argc, argv)));
    }
    catch (const std::exception& e) {
        return throwError(ctx, std::string("method '") + binding->methodName + "' failed: " + e.what());
    }
}

} // namespace

struct JSScriptingSystem::Impl
{
    struct TypeSlot
    {
        ya::type_index_t typeIndex = 0;
        JSValue          proto     = JS_UNDEFINED;
    };

    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;

    std::unordered_map<std::string, TypeSlot>       classProtos;      // typeName -> slot
    std::unordered_map<ya::type_index_t, JSValue> protoByTypeIndex; // typeIndex -> proto

    ~Impl()
    {
        for (auto& [name, slot] : classProtos) {
            (void)name;
            JS_FreeValue(context, slot.proto);
        }
        classProtos.clear();
        protoByTypeIndex.clear();
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
JSScriptingSystem::JSScriptingSystem() = default;
JSScriptingSystem::~JSScriptingSystem() = default;

namespace
{

JSScriptingSystem::Impl* gScriptingImpl = nullptr;

JSValue wrapObject(JSContext* ctx, ScriptHandle* handle, JSValueConst proto)
{
    JSValue obj = JS_NewObjectClass(ctx, gWrapperClassId);
    JS_SetOpaque(obj, handle);
    if (!JS_IsUndefined(proto)) {
        JS_SetPrototype(ctx, obj, proto);
    }
    return obj;
}

/// Materializes a typed pointer to a reflected instance into a wrapped JS
/// object carrying the class prototype (components, Entity, Scene - all go
/// through this single path).
JSValue wrapInstance(JSContext* ctx, ya::type_index_t typeIndex, void* ptr)
{
    const auto it = gScriptingImpl->protoByTypeIndex.find(typeIndex);
    if (it == gScriptingImpl->protoByTypeIndex.end() || JS_IsUndefined(it->second)) {
        return JS_NULL;
    }
    return wrapObject(ctx, new ScriptHandle{typeIndex, ptr}, it->second);
}

/// JSON -> JS conversion that materializes opaque instance-ref markers
/// ({"$yaHandle": {typeIndex, ptr}}) into wrapped objects at any nesting
/// depth, so handle-returning reflected methods compose with plain data.
JSValue jsonToJsWithHandles(JSContext* ctx, const Json& json)
{
    if (json.is_object()) {
        if (const auto it = json.find(kInstanceRefJsonKey); it != json.end()) {
            const auto& ref       = *it;
            const auto  typeIndex = ref.value("typeIndex", ya::type_index_t{0});
            const auto  ptr       = ref.value("ptr", uintptr_t{0});
            return wrapInstance(ctx, typeIndex, reinterpret_cast<void*>(ptr));
        }

        JSValue obj = JS_NewObject(ctx);
        for (auto it2 = json.begin(); it2 != json.end(); ++it2) {
            JS_SetPropertyStr(ctx, obj, it2.key().c_str(), jsonToJsWithHandles(ctx, it2.value()));
        }
        return obj;
    }
    if (json.is_array()) {
        JSValue  array = JS_NewArray(ctx);
        uint32_t index = 0;
        for (const auto& element : json) {
            JS_SetPropertyUint32(ctx, array, index++, jsonToJsWithHandles(ctx, element));
        }
        return array;
    }
    return jsonToJs(ctx, json);
}

JSValue sceneActiveFunction(JSContext* ctx,
                            JSValueConst /*this_val*/,
                            int /*argc*/,
                            JSValueConst* /*argv*/)
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        return JS_NULL;
    }
    return wrapInstance(ctx, ya::type_index_v<Scene>, scene);
}

JSValue entityCreateFunction(JSContext* ctx,
                             JSValueConst /*this_val*/,
                             int argc,
                             JSValueConst* argv)
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        return throwError(ctx, "entity.create: no active scene");
    }
    const char* nameCStr = argc >= 1 ? JS_ToCString(ctx, argv[0]) : nullptr;
    const std::string name = nameCStr != nullptr ? nameCStr : "Entity";
    if (nameCStr != nullptr) {
        JS_FreeCString(ctx, nameCStr);
    }
    Node3D* const node = scene->createNode3D(name);
    if (node == nullptr || node->getEntity() == nullptr) {
        return throwError(ctx, "entity.create: failed to create entity");
    }
    return wrapInstance(ctx, ya::type_index_v<Entity>, node->getEntity());
}

JSValue entityGetFunction(JSContext* ctx,
                          JSValueConst /*this_val*/,
                          int argc,
                          JSValueConst* argv)
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
    return wrapInstance(ctx, ya::type_index_v<Entity>, entity);
}

JSValue entityListFunction(JSContext* ctx,
                           JSValueConst /*this_val*/,
                           int /*argc*/,
                           JSValueConst* /*argv*/)
{
    Scene* scene = ScriptApiRegistry::get().getActiveScene();
    if (scene == nullptr) {
        return throwError(ctx, "entity.list: no active scene");
    }
    JSValue array = JS_NewArray(ctx);
    uint32_t index = 0;
    for (auto& [handle, entity] : scene->_entityMap) {
        (void)handle;
        JS_SetPropertyUint32(ctx, array, index++, wrapInstance(ctx, ya::type_index_v<Entity>, &entity));
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

    Json   result;
    std::string error;
    if (!ScriptApiRegistry::get().invoke(binding->name, args, result, error)) {
        return throwError(ctx, error);
    }
    return jsonToJsWithHandles(ctx, result);
}

/// Creates `ya.<namespace>.<fn>` objects for every registered command.
void buildRegistryLibraryObjects(JSContext* ctx, JSValue yaGlobal)
{
    for (const auto& [name, info] : ScriptApiRegistry::get().functions()) {
        std::vector<std::string> parts;
        size_t start = 0;
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
        const std::string& leaf = parts.back();
        JSValue existing = JS_GetPropertyStr(ctx, current, leaf.c_str());
        const bool bShadowed = !JS_IsUndefined(existing);
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

/**
 * @brief ProtoBuilder - quickjspp-style chained class/prototype registration.
 *
 * Single place where the reflection auto-exporter emits methods + fields onto
 * a class prototype. Every reflected class (components, Entity, Scene) goes
 * through it - no hand-written per-class binding.
 */
class ProtoBuilder
{
  public:
    ProtoBuilder(JSContext* ctx, JSValue proto) : _ctx(ctx), _proto(proto) {}

    ProtoBuilder& jsMethod(const char* name,
                           JSCClosure            fn,
                           void*                 opaque,
                           JSCClosureFinalizerFunc* finalizer = nullptr,
                           int                   length    = 0)
    {
        JSValue f = JS_NewCClosure(_ctx, fn, name, finalizer, length, 0, opaque);
        JS_SetPropertyStr(_ctx, _proto, name, f);
        return *this;
    }

    ProtoBuilder& getter(const char* name, JSCClosure fn, void* opaque, JSCClosureFinalizerFunc* finalizer)
    {
        JSValue g = JS_NewCClosure(_ctx, fn, name, finalizer, 0, 0, opaque);
        JSAtom atom = JS_NewAtom(_ctx, name);
        JS_DefinePropertyGetSet(_ctx, _proto, atom, g, JS_UNDEFINED, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(_ctx, atom);
        return *this;
    }

    // Reflected member function -> prototype method (auto-exported).
    ProtoBuilder& reflectedMethod(ya::type_index_t typeIndex, const std::string& methodName)
    {
        return jsMethod(methodName.c_str(),
                        reflectedMethodTrampoline,
                        new MethodBinding{typeIndex, methodName},
                        methodBindingFinalizer);
    }

    // Reflected field -> get/set property (auto-exported).
    ProtoBuilder& reflectedField(ya::type_index_t typeIndex, const std::string& fieldName)
    {
        JSValue getter = JS_NewCClosure(_ctx,
                                        fieldGetterClosure,
                                        "get",
                                        fieldBindingFinalizer,
                                        0,
                                        0,
                                        new FieldBinding{typeIndex, fieldName});
        JSValue setter = JS_NewCClosure(_ctx,
                                        fieldSetterClosure,
                                        "set",
                                        fieldBindingFinalizer,
                                        0,
                                        0,
                                        new FieldBinding{typeIndex, fieldName});
        JSAtom atom = JS_NewAtom(_ctx, fieldName.c_str());
        JS_DefinePropertyGetSet(_ctx, _proto, atom, getter, setter, JS_PROP_C_W_E);
        JS_FreeAtom(_ctx, atom);
        return *this;
    }

  private:
    JSContext* _ctx;
    JSValue    _proto;
};

/// Binds one reflected class (component, Entity or Scene) to a JS prototype.
/// The single entry point for script class export - what a class exposes is
/// decided solely by its YA_REFLECT_* declarations.
void bindReflectedClass(JSScriptingSystem::Impl& impl, const std::string& typeName, ya::type_index_t typeIndex)
{
    auto& slot = impl.classProtos[typeName];
    slot.typeIndex = typeIndex;
    slot.proto = JS_NewObject(impl.context);
    impl.protoByTypeIndex[typeIndex] = slot.proto;

    ProtoBuilder builder(impl.context, slot.proto);

    // Reflected methods -> prototype functions (single source: Class::functions).
    const auto* cls = ClassRegistry::instance().getClass(typeIndex);
    if (cls != nullptr) {
        for (const auto& [name, fn] : cls->functions) {
            (void)fn;
            builder.reflectedMethod(typeIndex, name);
        }
        // Reflected fields -> get/set properties.
        for (const auto& [fieldName, prop] : cls->properties) {
            (void)prop;
            builder.reflectedField(typeIndex, fieldName);
        }
    }
}

} // namespace

void JSScriptingSystem::init()
{
    _impl = std::make_unique<Impl>();

    _impl->runtime = JS_NewRuntime();
    _impl->context = JS_NewContext(_impl->runtime);
    if (_impl->context == nullptr) {
        YA_CORE_ERROR("JSScriptingSystem: failed to create quickjs context");
        return;
    }

    // Core authoring API for the JSON/RPC transport (scene/entity/component).
    registerCoreScriptApis(ScriptApiRegistry::get());
    registerAssetScriptApis(ScriptApiRegistry::get());

    // Single wrapper class: every wrapped C++ object carries a ScriptHandle.
    JS_NewClassID(_impl->runtime, &gWrapperClassId);
    const JSClassDef wrapperClassDef{
        .class_name = "ya.Wrapper",
        .finalizer  = wrapperFinalizer,
    };
    JS_NewClass(_impl->runtime, gWrapperClassId, &wrapperClassDef);

    gScriptingImpl = _impl.get();

    // Auto-export every registered component type (fields + methods).
    for (const auto& [fname, typeIndex] : ECSRegistry::get().getTypeIndexCache()) {
        const std::string typeName = fname.toString();
        if (ClassRegistry::instance().getClass(typeIndex) != nullptr) {
            bindReflectedClass(*_impl, typeName, typeIndex);
        }
    }

    // Entity / Scene are reflected classes too (not ECS components): export
    // them through the exact same generic binder.
    bindReflectedClass(*_impl, "Entity", ya::type_index_v<Entity>);
    bindReflectedClass(*_impl, "Scene", ya::type_index_v<Scene>);

    // Global surface: ya.scene.active(), ya.entity.create/get/list, ya.__commands
    // Ownership of `ya` transfers to the global object via JS_SetPropertyStr.
    JSValue yaGlobal = JS_NewObject(_impl->context);
    JSValue global = JS_GetGlobalObject(_impl->context);
    JS_SetPropertyStr(_impl->context, global, "ya", yaGlobal);
    JS_FreeValue(_impl->context, global);

    JSValue sceneModule = JS_NewObject(_impl->context);
    JS_SetPropertyStr(_impl->context, sceneModule, "active",
                      JS_NewCFunction(_impl->context, sceneActiveFunction, "active", 0));
    JS_SetPropertyStr(_impl->context, yaGlobal, "scene", sceneModule);

    JSValue entityModule = JS_NewObject(_impl->context);
    JS_SetPropertyStr(_impl->context, entityModule, "create",
                      JS_NewCFunction(_impl->context, entityCreateFunction, "create", 1));
    JS_SetPropertyStr(_impl->context, entityModule, "get",
                      JS_NewCFunction(_impl->context, entityGetFunction, "get", 1));
    JS_SetPropertyStr(_impl->context, entityModule, "list",
                      JS_NewCFunction(_impl->context, entityListFunction, "list", 0));
    JS_SetPropertyStr(_impl->context, yaGlobal, "entity", entityModule);

    // Function libraries: every registered command becomes ya.<ns>.<fn>.
    buildRegistryLibraryObjects(_impl->context, yaGlobal);

    JS_SetPropertyStr(_impl->context, yaGlobal, "__commands",
                      jsonToJs(_impl->context, ScriptApiRegistry::get().buildCommandList()));
}

void JSScriptingSystem::shutdown()
{
    if (gScriptingImpl == _impl.get()) {
        gScriptingImpl = nullptr;
    }
    _impl.reset();
}

JSScriptingSystem::EvalResult JSScriptingSystem::evalJS(const std::string& source, const std::string& filename)
{
    EvalResult result;
    if (_impl == nullptr || _impl->context == nullptr) {
        result.error = "js scripting unavailable";
        return result;
    }

    JSValue value = JS_Eval(_impl->context,
                            source.c_str(),
                            source.size(),
                            filename.c_str(),
                            JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) {
        JSValue exception = JS_GetException(_impl->context);
        const char* message = JS_ToCString(_impl->context, exception);
        result.error = message != nullptr ? message : "unknown js error";
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

bool JSScriptingSystem::invoke(const std::string& name,
                               const ScriptApiRegistry::Json& args,
                               ScriptApiRegistry::Json&       outResult,
                               std::string&                   outError)
{
    return ScriptApiRegistry::get().invoke(name, args, outResult, outError);
}

} // namespace ya
