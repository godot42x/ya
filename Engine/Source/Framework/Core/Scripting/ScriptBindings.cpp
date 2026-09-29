#include "Core/Scripting/ScriptBindings.h"

#include <reflects-core/lib.h>

#include <any>
#include <format>
#include <type_traits>
#include <unordered_map>
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
};

/// Either a reflected method (`function` set) or a native one.
struct ScriptMethod
{
    std::string               name;
    const Function*           function = nullptr;
    std::vector<type_index_t> ownerPath;
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
    std::string           name;
    NameMap<ScriptField>  fields;
    /// Reflected, native and resolver-answered methods alike.
    NameMap<ScriptMethod> methods;
    ScriptMethodResolver  resolver;
};

struct State
{
    std::vector<ScriptRefKind>                   kinds;
    std::unordered_map<type_index_t, TypeExport> types;
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
        out.methods.insert_or_assign(name, ScriptMethod{.name = name, .function = &function, .ownerPath = path});
    }
}

TypeExport& exportOf(type_index_t type)
{
    TypeExport& entry = state().types[type];
    if (!entry.bBuilt) {
        entry.bBuilt = true;
        if (const Class* cls = ClassRegistry::instance().getClass(type)) {
            entry.name = cls->name;
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

} // namespace

uint32_t registerRefKind(ScriptRefKind kind)
{
    auto& kinds = state().kinds;
    kinds.push_back(std::move(kind));
    return static_cast<uint32_t>(kinds.size());
}

void addNativeMethod(type_index_t type, std::string name, ScriptNativeFn fn)
{
    TypeExport&  entry  = state().types[type];
    ScriptMethod method = {.name = name, .native = std::move(fn)};
    if (entry.bBuilt) {
        entry.methods.try_emplace(std::move(name), std::move(method));
    }
    else {
        // exportOf() lets reflected methods overwrite it later.
        entry.methods.insert_or_assign(std::move(name), std::move(method));
    }
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
    const TypeExport& entry = exportOf(type);
    const auto        it    = entry.fields.find(name);
    return it != entry.fields.end() ? &it->second : nullptr;
}

const ScriptMethod* findMethod(type_index_t type, std::string_view name)
{
    TypeExport& entry = exportOf(type);
    if (const auto it = entry.methods.find(name); it != entry.methods.end()) {
        return &it->second;
    }
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
    const Property& prop = *field.property;
    return loadValue(prop.typeIndex, prop.addressGetter(toOwner(resolve(ref), ref.type, field.ownerPath)));
}

void writeField(const ScriptRef& ref, const ScriptField& field, const ScriptValue& value)
{
    if (field.bReadOnly) {
        throw ScriptError(std::format("field '{}' is read-only", field.name));
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
    void*          self   = toOwner(resolve(ref), ref.type, method.ownerPath);
    const std::any result = function.invoker(self, boxed);
    return function.returnTypeIndex != 0 ? unboxValue(function.returnTypeIndex, result) : ScriptValue{};
}

ScriptValue readField(const ScriptRef& ref, std::string_view field)
{
    if (const ScriptField* found = findField(ref.type, field)) {
        return readField(ref, *found);
    }
    throw ScriptError(std::format("{} has no field '{}'", displayName(ref.type), field));
}

void writeField(const ScriptRef& ref, std::string_view field, const ScriptValue& value)
{
    if (const ScriptField* found = findField(ref.type, field)) {
        writeField(ref, *found, value);
        return;
    }
    throw ScriptError(std::format("{} has no field '{}'", displayName(ref.type), field));
}

ScriptValue callMethod(const ScriptRef& ref, std::string_view method, ScriptArgs args)
{
    if (const ScriptMethod* found = findMethod(ref.type, method)) {
        return callMethod(ref, *found, args);
    }
    throw ScriptError(std::format("{} has no method '{}'", displayName(ref.type), method));
}

} // namespace ya::script
