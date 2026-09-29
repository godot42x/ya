#include "Core/Scripting/ScriptBindings.h"

#include "Core/Log.h"
#include "Core/Reflection/MetadataSupport.h"

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

// ============================================================================
// Per-type export, built from reflection marks on first use.
// ============================================================================

struct StringHash
{
    using is_transparent = void;
    size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
};

template <typename T>
using NameMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

/// A marked plugin member plus the reflected classes between the exported
/// type and the member's owner (empty when the type declares it itself).
template <typename TMember>
struct Marked
{
    const TMember*            member = nullptr;
    std::vector<type_index_t> ownerPath;
};

struct TypeExport
{
    bool                     bBuilt = false;
    std::string              name;
    NameMap<Marked<Property>> fields;
    NameMap<Marked<Function>> methods;
    NameMap<ScriptNativeFn>   natives;
    ScriptMethodResolver      resolver;
    NameMap<ScriptNativeFn>   resolved;
};

struct State
{
    std::vector<ScriptRefKind>                 kinds;
    std::unordered_map<type_index_t, TypeExport> types;
};

State& state()
{
    static State instance;
    return instance;
}

std::string scriptNameOf(const Field& member)
{
    if (member.metadata.hasMeta(reflection::Meta::ScriptName)) {
        return member.metadata.get<std::string>(reflection::Meta::ScriptName);
    }
    const size_t first = member.name.find_first_not_of('_');
    return first == std::string::npos ? member.name : member.name.substr(first);
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

void collectMarked(const Class& cls, const std::vector<type_index_t>& path, TypeExport& out)
{
    for (const type_index_t parent : cls.parents) {
        if (const Class* parentClass = ClassRegistry::instance().getClass(parent)) {
            std::vector<type_index_t> parentPath = path;
            parentPath.push_back(parent);
            collectMarked(*parentClass, parentPath, out);
        }
    }

    for (const auto& [propertyName, property] : cls.properties) {
        const bool bMarked = property.metadata.hasFlag(FieldFlags::BlueprintReadWrite) ||
                             property.metadata.hasFlag(FieldFlags::BlueprintReadOnly);
        if (!bMarked) {
            continue;
        }
        if (property.bStatic || property.bPointer || !crossesToScripts(property.typeIndex)) {
            YA_CORE_WARN("[Script] {}.{} is marked for scripts but its type cannot cross to scripts; skipped",
                         cls.name,
                         propertyName);
            continue;
        }
        out.fields.insert_or_assign(scriptNameOf(property), Marked<Property>{&property, path});
    }

    for (const auto& [functionName, function] : cls.functions) {
        if (!function.metadata.hasFlag(FieldFlags::BlueprintCallable)) {
            continue;
        }
        if (!signatureCrosses(function)) {
            YA_CORE_WARN("[Script] {}::{} is marked for scripts but its signature cannot cross to scripts; skipped",
                         cls.name,
                         functionName);
            continue;
        }
        out.methods.insert_or_assign(scriptNameOf(function), Marked<Function>{&function, path});
    }
}

TypeExport& exportOf(type_index_t type)
{
    TypeExport& entry = state().types[type];
    if (!entry.bBuilt) {
        entry.bBuilt = true;
        if (const Class* cls = ClassRegistry::instance().getClass(type)) {
            entry.name = cls->name;
            collectMarked(*cls, {}, entry);
        }
    }
    return entry;
}

template <typename T>
const T* findIn(const NameMap<T>& map, std::string_view name)
{
    const auto it = map.find(name);
    return it != map.end() ? &it->second : nullptr;
}

const ScriptNativeFn* findNative(TypeExport& entry, std::string_view name)
{
    if (const ScriptNativeFn* native = findIn(entry.natives, name)) {
        return native;
    }
    if (const ScriptNativeFn* resolved = findIn(entry.resolved, name)) {
        return resolved;
    }
    if (!entry.resolver) {
        return nullptr;
    }
    std::optional<ScriptNativeFn> fn = entry.resolver(name);
    if (!fn) {
        return nullptr;
    }
    return &entry.resolved.insert_or_assign(std::string(name), std::move(*fn)).first->second;
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

const Marked<Property>& fieldOf(type_index_t type, std::string_view field)
{
    const Marked<Property>* marked = findIn(exportOf(type).fields, field);
    if (!marked) {
        throw ScriptError(std::format("{} has no field '{}'", displayName(type), field));
    }
    return *marked;
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
    state().types[type].natives.insert_or_assign(std::move(name), std::move(fn));
}

void setMethodResolver(type_index_t type, ScriptMethodResolver resolver)
{
    TypeExport& entry = state().types[type];
    entry.resolver    = std::move(resolver);
    entry.resolved.clear();
}

std::string scriptTypeName(type_index_t type)
{
    return exportOf(type).name;
}

EScriptMember findMember(type_index_t type, std::string_view name)
{
    TypeExport& entry = exportOf(type);
    if (findIn(entry.fields, name)) {
        return EScriptMember::Field;
    }
    if (findIn(entry.methods, name) || findNative(entry, name)) {
        return EScriptMember::Method;
    }
    return EScriptMember::None;
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

ScriptValue readField(const ScriptRef& ref, std::string_view field)
{
    const Marked<Property>& marked = fieldOf(ref.type, field);
    const Property&         prop   = *marked.member;
    return loadValue(prop.typeIndex, prop.addressGetter(toOwner(resolve(ref), ref.type, marked.ownerPath)));
}

void writeField(const ScriptRef& ref, std::string_view field, const ScriptValue& value)
{
    const Marked<Property>& marked = fieldOf(ref.type, field);
    const Property&         prop   = *marked.member;
    if (prop.metadata.hasFlag(FieldFlags::BlueprintReadOnly) || prop.bConst || !prop.addressGetterMutable) {
        throw ScriptError(std::format("field '{}' is read-only", field));
    }
    void* object = resolve(ref);
    storeValue(prop.typeIndex, prop.addressGetterMutable(toOwner(object, ref.type, marked.ownerPath)), value);
    if (const auto& afterWrite = kindOf(ref)->afterWrite) {
        afterWrite(ref, object);
    }
}

ScriptValue callMethod(const ScriptRef& ref, std::string_view method, ScriptArgs args)
{
    TypeExport& entry = exportOf(ref.type);
    if (const Marked<Function>* marked = findIn(entry.methods, method)) {
        const Function& function = *marked->member;
        if (args.size() != function.argTypeIndices.size()) {
            throw ScriptError(std::format("{}() expects {} argument(s), got {}", method, function.argTypeIndices.size(), args.size()));
        }
        ArgumentList boxed;
        boxed.args.reserve(args.size());
        for (size_t index = 0; index < args.size(); ++index) {
            boxed.args.push_back(boxValue(function.argTypeIndices[index], args[index]));
        }
        void*          self   = toOwner(resolve(ref), ref.type, marked->ownerPath);
        const std::any result = function.invoker(self, boxed);
        return function.returnTypeIndex != 0 ? unboxValue(function.returnTypeIndex, result) : ScriptValue{};
    }
    if (const ScriptNativeFn* native = findNative(entry, method)) {
        return (*native)(resolve(ref), ref, args);
    }
    throw ScriptError(std::format("{} has no method '{}'", displayName(ref.type), method));
}

} // namespace ya::script
