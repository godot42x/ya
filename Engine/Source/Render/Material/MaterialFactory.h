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
    static MaterialFactoryInternal *_instance;

    std::unordered_map<uint32_t, std::vector<std::shared_ptr<Material>>> _materials;
    std::unordered_map<FName, Material *>                                _materialNameMap;

    uint32_t _materialCount = 0;

  public:
    static void                     init();
    static MaterialFactoryInternal *get() { return _instance; }

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
        uint32_t index  = 0;

        auto it = _materials.find(typeID);
        if (it == _materials.end())
        {
            _materials.insert({typeID, {mat}});
        }
        else
        {
            index = it->second.size();
            _materials[typeID].push_back(mat);
        }
        _materialCount += 1;
        // index: 该类型材质的第n个instance, 从0开始
        auto typeRef = static_cast<Material *>(mat.get());
        typeRef->setIndex(static_cast<int32_t>(index));
        typeRef->setTypeID(typeID);
        return mat.get();
    }
    // TODO:
    // 1. use a dirty marker system to delay destroy, batch destroy for vector erase efficiency
    // 2. use pool reuse freed slot
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
                matVec.erase(matVec.begin() + index);
                // Update indices of subsequent materials
                for (size_t i = index; i < matVec.size(); ++i)
                {
                    matVec[i]->setIndex(static_cast<int32_t>(i));
                }
                _materialCount -= 1;
            }
        }
    }
};
} // namespace detail



using MaterialFactory = detail::MaterialFactoryInternal<ya::TypeIndex>;


} // namespace ya