#include "Core/Scripting/ScriptBindings.h"

#include "Core/Log.h"

#include <reflects-core/lib.h>

#include <algorithm>
#include <any>
#include <deque>
#include <format>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ya::script
{

namespace
{

// ============================================================================
// Value codecs: ScriptValue <-> a C++ value of one type, in place (fields) or
// boxed in std::any (Function arguments and results).
// ============================================================================

template <typename T>
T fromScript(const ScriptValue& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return scriptToBool(value);
    }
    else if constexpr (std::is_integral_v<T>) {
        return static_cast<T>(scriptToInteger(value));
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(scriptToNumber(value));
    }
    else if constexpr (std::is_same_v<T, std::string>) {
        return scriptToString(value);
    }
    else if constexpr (std::is_same_v<T, ScriptRef>) {
        if (const ScriptRef* ref = std::get_if<ScriptRef>(&value)) {
            return *ref;
        }
        if (std::holds_alternative<std::monostate>(value)) {
            return {};
        }
        throw ScriptError(std::string("expected an object, got ") + scriptValueTypeName(value));
    }
    else {
        if (const T* v = std::get_if<T>(&value)) {
            return *v;
        }
        throw ScriptError(std::string("expected a vector, got ") + scriptValueTypeName(value));
    }
}

template <typename T>
ScriptValue toScript(const T& value)
{
    if constexpr (std::is_same_v<T, bool>) {
        return value;
    }
    else if constexpr (std::is_integral_v<T>) {
        return static_cast<int64_t>(value);
    }
    else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<double>(value);
    }
    else if constexpr (std::is_same_v<T, ScriptRef>) {
        return value ? ScriptValue{value} : ScriptValue{};
    }
    else {
        return value;
    }
}

struct ValueCodec
{
    ScriptValue (*load)(const void* address);
    void (*store)(void* address, const ScriptValue& value);
    std::any (*box)(const ScriptValue& value);
    ScriptValue (*unbox)(const std::any& boxed);
};

template <typename T>
ValueCodec codecOf()
{
    return {
        .load  = [](const void* address) { return toScript(*static_cast<const T*>(address)); },
        .store = [](void* address, const ScriptValue& value) { *static_cast<T*>(address) = fromScript<T>(value); },
        .box   = [](const ScriptValue& value) { return std::any(fromScript<T>(value)); },
        .unbox = [](const std::any& boxed) { return toScript(std::any_cast<const T&>(boxed)); },
    };
}

const std::unordered_map<type_index_t, ValueCodec>& valueCodecs()
{
    // Duplicate keys (int64_t is long or long long depending on the platform)
    // keep the first entry, which converts identically.
    static const std::unordered_map<type_index_t, ValueCodec> codecs = {
        {type_index_v<bool>, codecOf<bool>()},
        {type_index_v<int8_t>, codecOf<int8_t>()},
        {type_index_v<uint8_t>, codecOf<uint8_t>()},
        {type_index_v<int16_t>, codecOf<int16_t>()},
        {type_index_v<uint16_t>, codecOf<uint16_t>()},
        {type_index_v<int32_t>, codecOf<int32_t>()},
        {type_index_v<uint32_t>, codecOf<uint32_t>()},
        {type_index_v<long>, codecOf<long>()},
        {type_index_v<unsigned long>, codecOf<unsigned long>()},
        {type_index_v<long long>, codecOf<long long>()},
        {type_index_v<unsigned long long>, codecOf<unsigned long long>()},
        {type_index_v<float>, codecOf<float>()},
        {type_index_v<double>, codecOf<double>()},
        {type_index_v<std::string>, codecOf<std::string>()},
        {type_index_v<glm::vec2>, codecOf<glm::vec2>()},
        {type_index_v<glm::vec3>, codecOf<glm::vec3>()},
        {type_index_v<glm::vec4>, codecOf<glm::vec4>()},
        {type_index_v<ScriptRef>, codecOf<ScriptRef>()},
    };
    return codecs;
}

const ValueCodec* codecFor(type_index_t type)
{
    const auto it = valueCodecs().find(type);
    return it != valueCodecs().end() ? &it->second : nullptr;
}

const Enum* enumFor(type_index_t type)
{
    return EnumRegistry::instance().getEnum(type);
}

bool crossesToScripts(type_index_t type)
{
    return codecFor(type) || enumFor(type);
}

ScriptValue loadValue(type_index_t type, const void* address)
{
    if (const ValueCodec* codec = codecFor(type)) {
        return codec->load(address);
    }
    return enumFor(type)->getValue(const_cast<void*>(address));
}

void storeValue(type_index_t type, void* address, const ScriptValue& value)
{
    if (const ValueCodec* codec = codecFor(type)) {
        codec->store(address, value);
        return;
    }
    enumFor(type)->setValue(address, scriptToEnum(type, value));
}

std::any boxValue(type_index_t type, const ScriptValue& value)
{
    if (const ValueCodec* codec = codecFor(type)) {
        return codec->box(value);
    }
    return enumFor(type)->boxValue(scriptToEnum(type, value));
}

ScriptValue unboxValue(type_index_t type, const std::any& boxed)
{
    if (const ValueCodec* codec = codecFor(type)) {
        return codec->unbox(boxed);
    }
    return enumFor(type)->unboxValue(boxed);
}

} // namespace

// ============================================================================
// Per-type export, built from reflection on first use.
// ============================================================================

struct ScriptField
{
    std::string               name;
    const Property*           property = nullptr;
    /// Reflected classes between the exported type and the declaring class
    /// (empty when the type declares the field itself).
    std::vector<type_index_t> ownerPath;
    bool                      bReadOnly = false;
    std::function<ScriptValue(void*, const ScriptRef&)>              nativeRead;
    std::function<void(void*, const ScriptRef&, const ScriptValue&)> nativeWrite;
};

/// Either a reflected method (`function` set) or a native one.
struct ScriptMethod
{
    std::string               name;
    const Function*           function = nullptr;
    std::vector<type_index_t> ownerPath;
    /// Script-facing parameter types of a native method. Reflected methods
    /// use `function->argTypeIndices` instead.
    std::vector<type_index_t> argTypes;
    ScriptNativeFn            native;
};

namespace
{

struct StringHash
{
    using is_transparent = void;
    size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

template <typename T>
using NameMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

/// Node-based maps that only ever grow: handles into them never move.
struct TypeExport
{
    bool                  bBuilt = false;
    bool                  bNameLocked = false;
    std::string           name;
    NameMap<ScriptField>  fields;
    /// Reflected, native and resolver-answered methods alike.
    NameMap<ScriptMethod> methods;
    ScriptMethodResolver  resolver;
    /// 0: no script base. Lookup continues here when this type's table misses.
    type_index_t          base = 0;
};

struct State
{
    std::vector<ScriptRefKind>                   kinds;
    std::unordered_map<type_index_t, TypeExport> types;
    std::deque<ScriptModuleFunction>             moduleFunctions;
};

State& state()
{
    static State instance;
    return instance;
}

std::string scriptNameOf(const std::string& reflectedName)
{
    const size_t first = reflectedName.find_first_not_of('_');
    return first == std::string::npos ? reflectedName : reflectedName.substr(first);
}

bool signatureCrosses(const Function& function)
{
    if (function.isStatic() || (function.returnTypeIndex != 0 && !crossesToScripts(function.returnTypeIndex))) {
        return false;
    }
    for (const type_index_t arg : function.argTypeIndices) {
        if (!crossesToScripts(arg)) {
            return false;
        }
    }
    return true;
}

/// Members declared further down the hierarchy win over inherited ones of the
/// same script name.
void collectMembers(const Class& cls, const std::vector<type_index_t>& path, TypeExport& out)
{
    for (const type_index_t parent : cls.parents) {
        if (const Class* parentClass = ClassRegistry::instance().getClass(parent)) {
            std::vector<type_index_t> parentPath = path;
            parentPath.push_back(parent);
            collectMembers(*parentClass, parentPath, out);
        }
    }

    for (const auto& [propertyName, property] : cls.properties) {
        if (property.bStatic || property.bPointer || !crossesToScripts(property.typeIndex)) {
            continue;
        }
        std::string name = scriptNameOf(propertyName);
        // A native field already registered under this name keeps it. Widget
        // `text` / `name` go through setters; the reflected member would not.
        if (const auto existing = out.fields.find(name); existing != out.fields.end() && !existing->second.property) {
            continue;
        }
        out.fields.insert_or_assign(name,
                                    ScriptField{
                                        .name      = name,
                                        .property  = &property,
                                        .ownerPath = path,
                                        .bReadOnly = property.bConst || !property.addressGetterMutable,
                                    });
    }

    for (const auto& [functionName, function] : cls.functions) {
        if (!signatureCrosses(function)) {
            continue;
        }
        std::string name = scriptNameOf(functionName);
        if (const auto existing = out.methods.find(name); existing != out.methods.end() && !existing->second.function) {
            continue;
        }
        out.methods.insert_or_assign(name, ScriptMethod{.name = name, .function = &function, .ownerPath = path});
    }
}

TypeExport& exportOf(type_index_t type)
{
    TypeExport& entry = state().types[type];
    if (!entry.bBuilt) {
        entry.bBuilt = true;
        if (const Class* cls = ClassRegistry::instance().getClass(type)) {
            if (!entry.bNameLocked) {
                entry.name = cls->name;
            }
            collectMembers(*cls, {}, entry);
        }
    }
    return entry;
}

void* toOwner(void* object, type_index_t type, const std::vector<type_index_t>& ownerPath)
{
    for (const type_index_t parent : ownerPath) {
        const Class* cls = object ? ClassRegistry::instance().getClass(type) : nullptr;
        object           = cls ? cls->getParentPointer(object, parent) : nullptr;
        type             = parent;
    }
    if (!object) {
        throw ScriptError("reflected base class is not reachable from this object");
    }
    return object;
}

const ScriptRefKind* kindOf(const ScriptRef& ref)
{
    const auto& kinds = state().kinds;
    return ref.kind != 0 && ref.kind <= kinds.size() ? &kinds[ref.kind - 1] : nullptr;
}

std::string displayName(type_index_t type)
{
    const std::string& name = exportOf(type).name;
    return name.empty() ? std::string("object") : name;
}

bool isMissing(const ScriptRef& ref)
{
    const ScriptRefKind* kind = kindOf(ref);
    return kind && kind->missingIsNil && !tryResolve(ref);
}

void warnMissingOnce(const ScriptRef& ref)
{
    struct Key
    {
        uint32_t kind = 0;
        uint64_t a    = 0;
        bool     operator==(const Key&) const = default;
    };
    struct Hash
    {
        size_t operator()(const Key& key) const
        {
            return std::hash<uint64_t>{}(key.a) ^ (static_cast<size_t>(key.kind) << 1);
        }
    };
    static std::unordered_set<Key, Hash> seen;
    if (!seen.insert(Key{.kind = ref.kind, .a = ref.a}).second) {
        return;
    }
    const ScriptRefKind* kind = kindOf(ref);
    YA_CORE_WARN("script reference: operation on a gone {} {}; ignored", kind ? kind->name : "object", ref.a);
}

} // namespace

uint32_t registerRefKind(ScriptRefKind kind)
{
    auto& kinds = state().kinds;
    kinds.push_back(std::move(kind));
    return static_cast<uint32_t>(kinds.size());
}

void registerModuleFunction(std::string module, std::string name, ScriptFunction fn)
{
    auto&      functions = state().moduleFunctions;
    const auto existing  = std::ranges::find_if(functions, [&](const ScriptModuleFunction& entry) {
        return entry.module == module && entry.name == name;
    });
    if (existing != functions.end()) {
        existing->fn = std::move(fn);
        return;
    }
    functions.push_back(ScriptModuleFunction{.module = std::move(module), .name = std::move(name), .fn = std::move(fn)});
}

void forEachModuleFunction(const std::function<void(const ScriptModuleFunction&)>& visit)
{
    for (const ScriptModuleFunction& entry : state().moduleFunctions) {
        visit(entry);
    }
}

void addNativeMethod(type_index_t type, std::string name, ScriptNativeFn fn, std::vector<type_index_t> argTypes)
{
    TypeExport&  entry  = state().types[type];
    ScriptMethod method = {.name = name, .argTypes = std::move(argTypes), .native = std::move(fn)};
    if (entry.bBuilt) {
        // Already exported: the first registration wins.
        entry.methods.try_emplace(name, std::move(method));
    }
    else {
        // Pre-export: a later registration replaces an earlier one of the
        // same name. At export, collectMembers keeps an already-registered
        // native; a reflected method does not overwrite it.
        entry.methods.insert_or_assign(std::move(name), std::move(method));
    }
}

void addNativeField(type_index_t type, std::string name, ScriptNativeField field)
{
    TypeExport& entry = state().types[type];
    const bool  bReadOnly = !field.write;
    if (const auto it = entry.fields.find(name); it != entry.fields.end()) {
        if (!it->second.property) {
            return;
        }
        it->second.property = nullptr;
        it->second.ownerPath.clear();
        it->second.bReadOnly   = bReadOnly;
        it->second.nativeRead  = std::move(field.read);
        it->second.nativeWrite = std::move(field.write);
        return;
    }
    ScriptField scriptField{
        .name        = name,
        .property    = nullptr,
        .ownerPath   = {},
        .bReadOnly   = bReadOnly,
        .nativeRead  = std::move(field.read),
        .nativeWrite = std::move(field.write),
    };
    entry.fields.emplace(std::move(name), std::move(scriptField));
}

void addScriptBase(type_index_t derived, type_index_t base)
{
    if (derived == 0 || base == 0 || derived == base) {
        return;
    }
    TypeExport& entry = state().types[derived];
    if (entry.base == 0) {
        entry.base = base;
    }
}

void setScriptTypeName(type_index_t type, std::string name)
{
    TypeExport& entry = state().types[type];
    entry.name        = std::move(name);
    entry.bNameLocked = true;
}

void setMethodResolver(type_index_t type, ScriptMethodResolver resolver)
{
    state().types[type].resolver = std::move(resolver);
}

std::string scriptTypeName(type_index_t type)
{
    return exportOf(type).name;
}

const ScriptField* findField(type_index_t type, std::string_view name)
{
    type_index_t current = type;
    for (int depth = 0; current != 0 && depth < 8; ++depth) {
        TypeExport& entry = exportOf(current);
        if (const auto it = entry.fields.find(name); it != entry.fields.end()) {
            return &it->second;
        }
        const type_index_t base = entry.base;
        if (base == 0 || base == current) {
            break;
        }
        current = base;
    }
    return nullptr;
}

type_index_t methodArgumentType(const ScriptMethod& method, size_t index)
{
    if (method.function) {
        const auto& types = method.function->argTypeIndices;
        return index < types.size() ? types[index] : type_index_t{0};
    }
    return index < method.argTypes.size() ? method.argTypes[index] : type_index_t{0};
}

const ScriptMethod* findMethod(type_index_t type, std::string_view name)
{
    type_index_t current = type;
    for (int depth = 0; current != 0 && depth < 8; ++depth) {
        TypeExport& entry = exportOf(current);
        if (const auto it = entry.methods.find(name); it != entry.methods.end()) {
            return &it->second;
        }
        const type_index_t base = entry.base;
        if (base == 0 || base == current) {
            break;
        }
        current = base;
    }
    TypeExport& entry = exportOf(type);
    if (!entry.resolver) {
        return nullptr;
    }
    std::optional<ScriptNativeFn> fn = entry.resolver(name);
    if (!fn) {
        return nullptr;
    }
    std::string key(name);
    return &entry.methods.try_emplace(key, ScriptMethod{.name = key, .native = std::move(*fn)}).first->second;
}

void* tryResolve(const ScriptRef& ref)
{
    const ScriptRefKind* kind = kindOf(ref);
    return kind ? kind->resolve(ref) : nullptr;
}

void* resolve(const ScriptRef& ref)
{
    if (void* object = tryResolve(ref)) {
        return object;
    }
    const ScriptRefKind* kind = kindOf(ref);
    throw ScriptError(std::format("the {} this script refers to no longer exists", kind ? kind->name : "object"));
}

ScriptValue readField(const ScriptRef& ref, const ScriptField& field)
{
    // Native readers run even when the object is gone. Widget `valid` returns
    // false in that case; other readers return nil themselves. A reflected
    // field still short-circuits to nil.
    if (!field.property && field.nativeRead) {
        void* object = tryResolve(ref);
        if (!object) {
            if (const ScriptRefKind* kind = kindOf(ref); kind && kind->missingIsNil) {
                return field.nativeRead(nullptr, ref);
            }
            object = resolve(ref);
        }
        return field.nativeRead(object, ref);
    }
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return {};
    }
    if (!field.property) {
        throw ScriptError(std::format("field '{}' cannot be read", field.name));
    }
    const Property& prop = *field.property;
    return loadValue(prop.typeIndex, prop.addressGetter(toOwner(resolve(ref), ref.type, field.ownerPath)));
}

void writeField(const ScriptRef& ref, const ScriptField& field, const ScriptValue& value)
{
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return;
    }
    if (field.bReadOnly) {
        throw ScriptError(std::format("field '{}' is read-only", field.name));
    }
    if (!field.property) {
        void* object = resolve(ref);
        field.nativeWrite(object, ref, value);
        if (const auto& afterWrite = kindOf(ref)->afterWrite) {
            afterWrite(ref, object);
        }
        return;
    }
    const Property& prop   = *field.property;
    void*           object = resolve(ref);
    storeValue(prop.typeIndex, prop.addressGetterMutable(toOwner(object, ref.type, field.ownerPath)), value);
    if (const auto& afterWrite = kindOf(ref)->afterWrite) {
        afterWrite(ref, object);
    }
}

ScriptValue callMethod(const ScriptRef& ref, const ScriptMethod& method, ScriptArgs args)
{
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return {};
    }
    if (!method.function) {
        return method.native(resolve(ref), ref, args);
    }
    const Function& function = *method.function;
    if (args.size() != function.argTypeIndices.size()) {
        throw ScriptError(std::format("{}() expects {} argument(s), got {}", method.name, function.argTypeIndices.size(), args.size()));
    }
    ArgumentList boxed;
    boxed.args.reserve(args.size());
    for (size_t index = 0; index < args.size(); ++index) {
        boxed.args.push_back(boxValue(function.argTypeIndices[index], args[index]));
    }
    void*          object = resolve(ref);
    void*          self   = toOwner(object, ref.type, method.ownerPath);
    const std::any result = function.invoker(self, boxed);
    // Const is the reflected member-pointer kind (Function::isConst), not a
    // hand flag. Native methods have no such record and stay quiet.
    if (!function.isConst()) {
        if (const auto& afterCall = kindOf(ref)->afterCall) {
            afterCall(ref, object);
        }
    }
    return function.returnTypeIndex != 0 ? unboxValue(function.returnTypeIndex, result) : ScriptValue{};
}

ScriptValue readField(const ScriptRef& ref, std::string_view field)
{
    if (const ScriptField* found = findField(ref.type, field)) {
        return readField(ref, *found);
    }
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return {};
    }
    throw ScriptError(std::format("{} has no field '{}'", displayName(ref.type), field));
}

void writeField(const ScriptRef& ref, std::string_view field, const ScriptValue& value)
{
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return;
    }
    if (const ScriptField* found = findField(ref.type, field)) {
        writeField(ref, *found, value);
        return;
    }
    throw ScriptError(std::format("{} has no field '{}'", displayName(ref.type), field));
}

ScriptValue callMethod(const ScriptRef& ref, std::string_view method, ScriptArgs args)
{
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return {};
    }
    if (const ScriptMethod* found = findMethod(ref.type, method)) {
        return callMethod(ref, *found, args);
    }
    throw ScriptError(std::format("{} has no method '{}'", displayName(ref.type), method));
}

namespace
{

IScriptCallbacks*& callbackSlot()
{
    static IScriptCallbacks* callbacks = nullptr;
    return callbacks;
}

} // namespace

void setScriptCallbacks(IScriptCallbacks* callbacks)
{
    callbackSlot() = callbacks;
}

IScriptCallbacks* scriptCallbacks()
{
    return callbackSlot();
}

} // namespace ya::script
