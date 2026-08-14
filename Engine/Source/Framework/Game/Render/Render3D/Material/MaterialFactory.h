#pragma once
#include "Core/FName.h"
#include "Material.h"


namespace ya
{


// MARK: Factory
namespace detail
{

// struct MaterialStores
// {
//     std::vector<stdptr<Material>> materials;
// };

template <template <typename> typename HashFunctor>
struct MaterialFactoryInternal
{
    // NOTE: the static data member is deliberately NOT declared dllexport here.
    // Data symbols cannot be "exported from elsewhere": with the module export
    // macro propagated into consuming DLLs, MSVC would require _instance to be
    // defined inside every consumer and linking fails (LNK2001). It is exported
    // from its single definition site in MaterialFactory.cpp instead.
    static MaterialFactoryInternal *_instance;

    std::unordered_map<uint32_t, std::vector<std::shared_ptr<Material>>> _materials;
    std::unordered_map<FName, Material *>                                _materialNameMap;

    uint32_t _materialCount = 0;

  public:
    static void                     init();
    /// Defined in MaterialFactory.cpp: returns the module-local singleton.
    /// Routed through a function (not an inline read of _instance) so the
    /// static data member never crosses the DLL boundary (a dllexport data
    /// symbol cannot be imported from another DLL while the module export macro
    /// propagates into consumers).
    static MaterialFactoryInternal *get();

    void destroy();


    template <typename T>
        requires std::is_base_of_v<Material, T>
    [[nodiscard]] std::size_t getMaterialSize() const
    {
        uint32_t typeID = getTypeID<T>();

        auto it = _materials.find(typeID);
        if (it != _materials.end())
        {
            return it->second.size();
        }
        return 0;
    }



    template <typename T>
        requires std::is_base_of<Material, T>::value
    T *createMaterial(std::string label)
    {
        auto *mat = createMaterialImpl<T>();
        static_cast<Material *>(mat)->setLabel(label);
        _materialNameMap.insert({FName(label), mat});
        return mat;
    }

    template <typename T>
        requires std::is_base_of_v<Material, T>
    [[nodiscard]] const std::vector<std::shared_ptr<Material>> &getMaterials() const
    {
        if (auto it = _materials.find(getTypeID<T>()); it != _materials.end()) {
            return it->second;
        }
        static const std::vector<std::shared_ptr<Material>> empty;
        return empty;
    }

    Material *getMaterialByName(FName name)
    {
        auto it = _materialNameMap.find(name);
        if (it != _materialNameMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    auto getAllMaterials() const { return _materials; }



    template <typename T>
    [[nodiscard]] static constexpr uint32_t getTypeID()
    {
        return HashFunctor<T>::value();
    }

    void destroyMaterial(Material *material) { destroyMaterialImpl(material); }
    void removeMaterial(Material *material) { destroyMaterialImpl(material); }

    uint32_t getMaterialCount() const { return _materialCount; }


  private:
    MaterialFactoryInternal() = default;


    template <typename T>
        requires std::is_base_of<Material, T>::value
    T *createMaterialImpl()
    {
        uint32_t typeID = getTypeID<T>();
        auto     mat    = makeShared<T>();
        int32_t  index  = -1;

        auto it = _materials.find(typeID);
        if (it == _materials.end())
        {
            _materials.insert({typeID, {mat}});
            index = 0;
        }
        else
        {
            auto &matVec = it->second;
            // Scan for a null (freed) slot to reuse — keeps index stable
            for (size_t i = 0; i < matVec.size(); ++i)
            {
                if (!matVec[i])
                {
                    matVec[i] = mat;
                    index = static_cast<int32_t>(i);
                    break;
                }
            }
            if (index < 0)
            {
                index = static_cast<int32_t>(matVec.size());
                matVec.push_back(mat);
            }
        }
        _materialCount += 1;
        auto typeRef = static_cast<Material *>(mat.get());
        typeRef->setIndex(index);
        typeRef->setTypeID(typeID);
        return mat.get();
    }
    // Slot-based destroy: null the slot, never erase, never renumber.
    void destroyMaterialImpl(Material *material)
    {
        uint32_t typeID = material->getTypeID();
        int32_t  index  = material->getIndex();
        _materialNameMap.erase(FName(material->getLabel()));

        auto it = _materials.find(typeID);
        if (it != _materials.end())
        {
            auto &matVec = it->second;
            if (index >= 0 && static_cast<size_t>(index) < matVec.size())
            {
                matVec[index] = nullptr;  // mark slot as free
                _materialCount -= 1;
            }
        }
    }
};
} // namespace detail



using MaterialFactory = detail::MaterialFactoryInternal<ya::TypeIndex>;


} // namespace ya
