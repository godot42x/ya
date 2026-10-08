#include "type_ops.h"

namespace refl
{

TypeRegistry &TypeRegistry::instance()
{
    static TypeRegistry registry;
    return registry;
}

TypeRegistry::TypeRegistry()
{
    registerBuiltins();
}

void TypeRegistry::registerBuiltins()
{
    registerType<bool>("bool");
    registerType<char>("char");
    registerType<signed char>("signed char");
    registerType<unsigned char>("unsigned char");
    registerType<short>("short");
    registerType<unsigned short>("unsigned short");
    registerType<int>("int");
    registerType<unsigned int>("unsigned int");
    registerType<long>("long");
    registerType<unsigned long>("unsigned long");
    registerType<long long>("long long");
    registerType<unsigned long long>("unsigned long long");
    registerType<float>("float");
    registerType<double>("double");
    registerType<std::string>("std::string");
}

TypeOps &TypeRegistry::registerTypeRaw(TypeOps ops)
{
    if (ops.type == 0) {
        throw std::runtime_error("Reflection type index 0 is reserved");
    }

    if (const auto it = _byType.find(ops.type); it != _byType.end()) {
        if (it->second->name != ops.name) {
            throw std::runtime_error("Reflection type id collision between " + it->second->name + " and " + ops.name + ": " +
                                     std::string(it->second->compilerName) + " vs " + std::string(ops.compilerName));
        }
        return *it->second;
    }
    if (const auto it = _byName.find(ops.name); it != _byName.end()) {
        std::string existingCompiler;
        if (const auto existing = _byType.find(it->second); existing != _byType.end()) {
            existingCompiler = std::string(existing->second->compilerName);
        }
        throw std::runtime_error("Reflection type name collision on '" + ops.name + "': " + existingCompiler + " vs " +
                                 std::string(ops.compilerName));
    }

    const type_index_t type = ops.type;
    const std::string  name = ops.name;
    _byName.emplace(name, type);
    try {
        auto owned = std::make_unique<TypeOps>(std::move(ops));
        TypeOps &ref = *owned;
        _byType.emplace(type, std::move(owned));
        return ref;
    }
    catch (...) {
        _byName.erase(name);
        throw;
    }
}

void TypeRegistry::unregisterType(type_index_t type)
{
    const auto it = _byType.find(type);
    if (it == _byType.end()) {
        return;
    }
    _byName.erase(it->second->name);
    _byType.erase(it);
}

const TypeOps *TypeRegistry::find(type_index_t type) const
{
    const auto it = _byType.find(type);
    if (it == _byType.end()) {
        return nullptr;
    }
    return it->second.get();
}

const TypeOps *TypeRegistry::findByName(std::string_view name) const
{
    const auto it = _byName.find(std::string(name));
    if (it == _byName.end()) {
        return nullptr;
    }
    return find(it->second);
}

void TypeRegistry::setFacetErased(type_index_t type, type_index_t facetType, void *data, void (*destroy)(void *))
{
    const auto it = _byType.find(type);
    if (it == _byType.end()) {
        if (destroy != nullptr && data != nullptr) {
            destroy(data);
        }
        throw std::runtime_error("setFacet: type is not registered");
    }

    TypeOps::FacetEntry entry;
    entry.facetType = facetType;
    entry.data      = data;
    entry.destroy   = destroy;

    for (auto &slot : it->second->facets) {
        if (slot.facetType == facetType) {
            slot = std::move(entry);
            return;
        }
    }
    it->second->facets.push_back(std::move(entry));
}

const void *TypeRegistry::findFacet(type_index_t type, type_index_t facetType) const
{
    const auto it = _byType.find(type);
    if (it == _byType.end()) {
        return nullptr;
    }
    for (const auto &slot : it->second->facets) {
        if (slot.facetType == facetType) {
            return slot.data;
        }
    }
    return nullptr;
}

} // namespace refl
