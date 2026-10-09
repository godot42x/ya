#include "Core/Scripting/ScriptBindings.h"

#include "Core/Log.h"
#include "Core/Memory/AllocatorStats.h"
#include "Core/Reflection/ValueFacets.h"

#include <reflects-core/lib.h>

#include <algorithm>
#include <cstring>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ya::script
{

namespace
{

// ============================================================================
// Value codecs: ScriptValue <-> a C++ value of one type. Narrow integers and
// float go through MetaValue::convertTo (out of range throws, no truncation).
// Identity arms keep the existing scriptTo* error text.
// ============================================================================

struct ValueCodec
{
    ScriptValue (*load)(const void* address);
    void (*store)(void* address, const ScriptValue& value);
    refl::MetaValue (*box)(const ScriptValue& value);
};

template <typename T>
refl::MetaValue boxIdentity(const ScriptValue& value)
{
    return refl::MetaValue::make(detail::fromScript<T>(value));
}

// Same acceptance rules as MetaValue::convertTo for an int64 / double source,
// inlined so the script boundary does not cross into reflects-core or look up
// TypeOps on every call. scriptToInteger / scriptToNumber already produced
// that source, so there is no other source type here.
template <typename T>
bool narrowFromWide(int64_t value, T& out)
{
    if constexpr (std::is_signed_v<T>) {
        if (value < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
            value > static_cast<int64_t>(std::numeric_limits<T>::max())) {
            return false;
        }
    }
    else if (value < 0 || static_cast<uint64_t>(value) > static_cast<uint64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    out = static_cast<T>(value);
    return true;
}

template <typename T>
bool narrowFromWide(double value, T& out)
{
    if (!std::isfinite(value)) {
        return false;
    }
    if constexpr (std::is_same_v<T, double>) {
        out = value;
        return true;
    }
    else {
        const double limit = static_cast<double>(std::numeric_limits<float>::max());
        if (value > limit || value < -limit) {
            return false;
        }
        out = static_cast<T>(value);
        return std::isfinite(out);
    }
}

template <typename T>
bool widenToScript(T value, int64_t& out)
{
    if constexpr (std::is_signed_v<T> || sizeof(T) < sizeof(int64_t)) {
        out = static_cast<int64_t>(value);
        return true;
    }
    else if (static_cast<uint64_t>(value) > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    else {
        out = static_cast<int64_t>(value);
        return true;
    }
}

template <typename T>
bool widenToScript(T value, double& out)
{
    out = static_cast<double>(value);
    return std::isfinite(out);
}

template <typename T>
refl::MetaValue boxNarrow(const ScriptValue& value)
{
    T stored{};
    bool ok = false;
    if constexpr (std::is_integral_v<T>) {
        ok = narrowFromWide(scriptToInteger(value), stored);
    }
    else {
        ok = narrowFromWide(scriptToNumber(value), stored);
    }
    if (!ok) {
        throw ScriptError("value is out of range");
    }
    return refl::MetaValue::make(stored);
}

template <typename T>
ScriptValue loadNarrow(const void* address)
{
    const T value = *static_cast<const T*>(address);
    if constexpr (std::is_integral_v<T>) {
        int64_t stored = 0;
        if (!widenToScript(value, stored)) {
            throw ScriptError("value is out of range");
        }
        return scriptMake(stored);
    }
    else {
        double stored = 0;
        if (!widenToScript(value, stored)) {
            throw ScriptError("value is out of range");
        }
        return scriptMake(stored);
    }
}

template <typename T>
void storeNarrow(void* address, const ScriptValue& value)
{
    const refl::MetaValue boxed = boxNarrow<T>(value);
    *static_cast<T*>(address) = *boxed.tryGet<T>();
}

template <typename T>
ValueCodec codecOf()
{
    return {
        .load  = [](const void* address) { return detail::toScript(*static_cast<const T*>(address)); },
        .store = [](void* address, const ScriptValue& value) { *static_cast<T*>(address) = detail::fromScript<T>(value); },
        .box   = &boxIdentity<T>,
    };
}

template <typename T>
ValueCodec narrowCodec()
{
    return {
        .load  = &loadNarrow<T>,
        .store = &storeNarrow<T>,
        .box   = &boxNarrow<T>,
    };
}

const std::unordered_map<type_index_t, ValueCodec>& valueCodecs()
{
        // Identity codecs are the types a script sees directly. Narrowing
        // codecs use convertTo. Duplicate keys (int64_t aliasing long or
        // long long) keep the first, identity, entry.
        static const std::unordered_map<type_index_t, ValueCodec> codecs = [] {
            std::unordered_map<type_index_t, ValueCodec> out;
            out.emplace(type_index_v<bool>, codecOf<bool>());
            out.emplace(type_index_v<int64_t>, codecOf<int64_t>());
            out.emplace(type_index_v<double>, codecOf<double>());
            out.emplace(type_index_v<std::string>, codecOf<std::string>());
            out.emplace(type_index_v<glm::vec2>, codecOf<glm::vec2>());
            out.emplace(type_index_v<glm::vec3>, codecOf<glm::vec3>());
            out.emplace(type_index_v<glm::vec4>, codecOf<glm::vec4>());
            out.emplace(type_index_v<ScriptRef>, codecOf<ScriptRef>());
            out.emplace(type_index_v<ScriptFnHandle>, codecOf<ScriptFnHandle>());
            out.emplace(type_index_v<ScriptSelfHandle>, codecOf<ScriptSelfHandle>());
            out.emplace(type_index_v<int8_t>, narrowCodec<int8_t>());
            out.emplace(type_index_v<uint8_t>, narrowCodec<uint8_t>());
            out.emplace(type_index_v<int16_t>, narrowCodec<int16_t>());
            out.emplace(type_index_v<uint16_t>, narrowCodec<uint16_t>());
            out.emplace(type_index_v<int32_t>, narrowCodec<int32_t>());
            out.emplace(type_index_v<uint32_t>, narrowCodec<uint32_t>());
            out.emplace(type_index_v<float>, narrowCodec<float>());
            out.emplace(type_index_v<long>, narrowCodec<long>());
            out.emplace(type_index_v<unsigned long>, narrowCodec<unsigned long>());
            out.emplace(type_index_v<long long>, narrowCodec<long long>());
            out.emplace(type_index_v<unsigned long long>, narrowCodec<unsigned long long>());
            return out;
        }();
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

bool isValueStruct(type_index_t type)
{
    return refl::TypeRegistry::instance().facet<reflection::FValueStruct>(type) != nullptr;
}

ScriptValue loadValueStruct(type_index_t type, const void* address)
{
    return refl::MetaValue::ref(type, const_cast<void*>(address), true).materialize();
}

void storeValueStruct(type_index_t type, void* address, const ScriptValue& value)
{
    if (value.type() != type || value.data() == nullptr) {
        throw ScriptError(std::string("expected a reflected struct, got ") + scriptValueTypeName(value));
    }
    const refl::TypeOps* ops = refl::TypeRegistry::instance().find(type);
    if (ops == nullptr || !ops->bTriviallyCopyable) {
        throw ScriptError("reflected struct cannot be copied into a field");
    }
    std::memcpy(address, value.data(), ops->life.size);
}

ScriptValue loadValue(type_index_t type, const void* address)
{
    if (const ValueCodec* codec = codecFor(type)) {
        return codec->load(address);
    }
    if (isValueStruct(type)) {
        return loadValueStruct(type, address);
    }
    return scriptMake(enumFor(type)->getValue(const_cast<void*>(address)));
}

void storeValue(type_index_t type, void* address, const ScriptValue& value)
{
    if (const ValueCodec* codec = codecFor(type)) {
        codec->store(address, value);
        return;
    }
    if (isValueStruct(type)) {
        storeValueStruct(type, address, value);
        return;
    }
    enumFor(type)->setValue(address, scriptToEnum(type, value));
}

refl::MetaValue boxValue(type_index_t type, const ScriptValue& value)
{
    if (const ValueCodec* codec = codecFor(type)) {
        return codec->box(value);
    }
    if (isValueStruct(type)) {
        if (value.isRef()) {
            return value.materialize();
        }
        if (value.type() != type) {
            throw ScriptError(std::string("expected a reflected struct, got ") + scriptValueTypeName(value));
        }
        return value;
    }
    const Enum* reflected = enumFor(type);
    alignas(8) unsigned char bytes[8] = {};
    reflected->setValue(bytes, scriptToEnum(type, value));
    return refl::MetaValue::trivial(type, bytes, reflected->underlyingSize);
}

ScriptValue unboxValue(const refl::ParamDesc& ret, const refl::MetaValue& boxed)
{
    if (ret.type == 0 || boxed.isNull() || boxed.data() == nullptr) {
        return {};
    }
    if (ret.type == type_index_v<ScriptValue>) {
        if (const ScriptValue* held = boxed.tryGet<ScriptValue>()) {
            return *held;
        }
        throw ScriptError("expected a script value");
    }
    if (const ValueCodec* codec = codecFor(ret.type)) {
        return codec->load(boxed.data());
    }
    if (isValueStruct(ret.type)) {
        return boxed.type() == ret.type ? boxed : loadValueStruct(ret.type, boxed.data());
    }
    return scriptMake(enumFor(ret.type)->getValue(const_cast<void*>(boxed.data())));
}

} // namespace

bool crossesToScripts(type_index_t type)
{
    return codecFor(type) || enumFor(type) || isValueStruct(type);
}

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

/// Exported method. `function` is the ClassRegistry entry; script calls go
/// through its MetaValue invoker. `native` is only a `ScriptMethodResolver`
/// answer, which has no Function entry.
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

/// `ScriptValue` is the dynamic return marker, not a codec. Handle types cross
/// through the codecs above. A reflected value struct crosses through its fields.
bool acceptsScriptSignature(type_index_t type)
{
    return crossesToScripts(type) || type == type_index_v<ScriptValue>;
}

bool signatureCrosses(const Function& function)
{
    if (function.isStatic()) {
        return false;
    }
    // bRef is a non-const reference. const T& stays exportable and is copied
    // into a ScriptValue at this boundary. Pointers and non-const references
    // stay out of scripts (poke is the baseline case).
    const refl::ParamDesc& ret = function.ret;
    if (ret.bRef || ret.bPointer) {
        return false;
    }
    if (ret.type != 0 && !acceptsScriptSignature(ret.type)) {
        return false;
    }
    for (const refl::ParamDesc& arg : function.args) {
        if (arg.bRef || arg.bPointer || !acceptsScriptSignature(arg.type)) {
            return false;
        }
    }
    return true;
}

/// Drop reflected projections so the next lookup rebuilds them. Resolver
/// answers and native fields have no reflected pointer and stay.
void invalidateExports()
{
    for (auto& [type, entry] : state().types) {
        (void)type;
        std::erase_if(entry.methods, [](const auto& item) { return item.second.function != nullptr; });
        std::erase_if(entry.fields, [](const auto& item) { return item.second.property != nullptr; });
        entry.bBuilt = false;
    }
}

void watchRegistry()
{
    static const uint64_t listener = ClassRegistry::instance().addListener([](uint64_t) { invalidateExports(); });
    (void)listener;
}

/// Inheritance, override, and owner path come from `Class::resolveMembers`.
/// This pass only trims the script name and drops members that cannot cross.
/// A registry change clears `bBuilt` so the next lookup rebuilds.
void collectMembers(const Class& cls, TypeExport& out)
{
    const ResolvedMembers resolved = cls.resolveMembers();
    for (const std::string& propertyName : resolved.propertyOrder) {
        const ResolvedProperty& resolvedProperty = resolved.properties.at(propertyName);
        const Property&         property         = *resolvedProperty.property;
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
                                        .ownerPath = resolvedProperty.ownerPath,
                                        .bReadOnly = property.bConst || !property.addressGetterMutable,
                                    });
    }

    for (const std::string& functionName : resolved.functionOrder) {
        const ResolvedFunction& resolvedFunction = resolved.functions.at(functionName);
        const Function&         function         = *resolvedFunction.function;
        if (!signatureCrosses(function)) {
            continue;
        }
        std::string name = scriptNameOf(functionName);
        // A resolver answer stored on this name has no Function. Keep it.
        if (const auto existing = out.methods.find(name); existing != out.methods.end() && !existing->second.function) {
            continue;
        }
        out.methods.insert_or_assign(name, ScriptMethod{.name = name, .function = &function, .ownerPath = resolvedFunction.ownerPath});
    }
}

TypeExport& exportOf(type_index_t type)
{
    watchRegistry();
    TypeExport& entry = state().types[type];
    if (!entry.bBuilt) {
        entry.bBuilt = true;
        if (const Class* cls = ClassRegistry::instance().getClass(type)) {
            if (!entry.bNameLocked) {
                entry.name = cls->name;
            }
            collectMembers(*cls, entry);
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

void registerModuleFunction(std::string module, std::string name, ScriptFunction fn, std::vector<type_index_t> argumentTypes)
{
    auto&      functions = state().moduleFunctions;
    const auto existing  = std::ranges::find_if(functions, [&](const ScriptModuleFunction& entry) {
        return entry.module == module && entry.name == name;
    });
    if (existing != functions.end()) {
        existing->fn            = std::move(fn);
        existing->argumentTypes = std::move(argumentTypes);
        return;
    }
    functions.push_back(ScriptModuleFunction{
        .module        = std::move(module),
        .name          = std::move(name),
        .fn            = std::move(fn),
        .argumentTypes = std::move(argumentTypes),
    });
}

void forEachModuleFunction(const std::function<void(const ScriptModuleFunction&)>& visit)
{
    for (const ScriptModuleFunction& entry : state().moduleFunctions) {
        visit(entry);
    }
}

void warnDuplicateFunction(const Class& cls, const std::string& functionName)
{
    YA_CORE_WARN("script method '{}::{}' keeps the first registration", cls.name, functionName);
}

void ensureDuplicateHook()
{
    auto& registry = ClassRegistry::instance();
    if (!registry.duplicateFunctionHook) {
        registry.duplicateFunctionHook = &warnDuplicateFunction;
    }
}

void addMethod(type_index_t type, std::string name, NativeMethod method)
{
    watchRegistry();
    ensureDuplicateHook();
    Class* cls = ClassRegistry::instance().getClass(type);
    if (!cls) {
        YA_CORE_WARN("script method '{}' has no reflected class; not registered", name);
        return;
    }
    if (cls->typeIndex != type) {
        YA_CORE_WARN("script method '{}::{}' does not match the reflected type index; not registered", cls->name, name);
        return;
    }
    TypeExport& entry = state().types[type];
    if (cls->functions().contains(name)) {
        Function rejected;
        rejected.name = name;
        cls->insertFunction(std::move(rejected));
        return;
    }

    Function function;
    function.name           = name;
    function.type           = method.bConst ? FunctionType::ConstMemberFunction : FunctionType::MemberFunction;
    function.argCount       = method.args.size();
    function.bVariadic      = method.bVariadic;
    function.argTypeNames   = std::move(method.argTypeNames);
    function.returnTypeName = std::move(method.returnTypeName);
    function.ret            = method.ret;
    function.args           = std::move(method.args);
    function.invoker        = std::move(method.invoker);
    cls->insertFunction(std::move(function));
    if (entry.bBuilt) {
        const Function* stored = &cls->functions().at(name);
        entry.methods.try_emplace(name, ScriptMethod{.name = name, .function = stored});
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
    if (!method.function) {
        return {};
    }
    const auto& types = method.function->args;
    return index < types.size() ? types[index].type : type_index_t{0};
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
    // This is the single funnel every script call passes through -- the Lua
    // metamethod, the QuickJS binding and the automation RPC all land here --
    // so it is where a script's own allocations (boxed arguments, the return
    // value, whatever the script body allocates) stop landing in `untagged`.
    // Placed before the missing-ref early return so a call that fails to
    // resolve is still attributed to the subsystem that asked for it.
    YA_MEMORY_OWNER(Script);
    if (isMissing(ref)) {
        warnMissingOnce(ref);
        return {};
    }
    // Resolver answers have no Function. They keep the raw resolved object
    // and do not run afterCall.
    if (method.native || !method.function) {
        void* object = resolve(ref);
        void* self   = method.function ? toOwner(object, ref.type, method.ownerPath) : object;
        return method.native(self, ref, args);
    }
    const Function& function = *method.function;
    if (!function.bVariadic && args.size() != function.args.size()) {
        throw ScriptError(std::format("{}() expects {} argument(s), got {}", method.name, function.args.size(), args.size()));
    }
    // Construct only the arguments this call uses. A fixed 8-slot array
    // default-constructs MetaValues that the hot path never reads.
    constexpr size_t kInlineArgs = 8;
    alignas(refl::MetaValue) std::byte inlineStorage[sizeof(refl::MetaValue) * kInlineArgs];
    std::vector<refl::MetaValue> heapArgs;
    refl::MetaValue* slots = nullptr;
    size_t built = 0;
    const auto destroyInline = [&]() {
        for (size_t index = built; index > 0; --index) {
            slots[index - 1].~MetaValue();
        }
    };
    try {
        if (args.size() > kInlineArgs) {
            heapArgs.reserve(args.size());
            for (size_t index = 0; index < args.size(); ++index) {
                // Variadic arguments are already MetaValues from the language
                // backend. Boxing again would reject a nil owner slot.
                heapArgs.push_back(function.bVariadic ? refl::MetaValue(args[index])
                                                      : boxValue(function.args[index].type, args[index]));
            }
            slots = heapArgs.data();
        }
        else if (!args.empty()) {
            slots = reinterpret_cast<refl::MetaValue*>(inlineStorage);
            for (size_t index = 0; index < args.size(); ++index) {
                new (slots + index) refl::MetaValue(function.bVariadic ? refl::MetaValue(args[index])
                                                                       : boxValue(function.args[index].type, args[index]));
                built = index + 1;
            }
        }
    }
    catch (...) {
        if (built != 0) {
            destroyInline();
        }
        throw;
    }
    void* object = resolve(ref);
    void* self   = toOwner(object, ref.type, method.ownerPath);
    refl::MetaValue result = [&]() -> refl::MetaValue {
        try {
            return function.invoker(self, std::span<refl::MetaValue>(slots, args.size()));
        }
        catch (...) {
            if (built != 0) {
                destroyInline();
            }
            throw;
        }
    }();
    if (built != 0) {
        destroyInline();
    }
    // Const is the reflected member-pointer kind (Function::isConst).
    // Resolver natives return above and do not reach here.
    if (!function.isConst()) {
        if (const auto& afterCall = kindOf(ref)->afterCall) {
            afterCall(ref, object);
        }
    }
    return unboxValue(function.ret, result);
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
