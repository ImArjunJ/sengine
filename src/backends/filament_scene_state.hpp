#pragma once
#include "filament_access.hpp"
#include "filament_environment.hpp"
#include "sengine/scene.hpp"
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/MorphTargetBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/FilamentInstance.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/TextureProvider.h>
#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>
namespace sengine {
struct scene::impl {
    filament::Engine& engine;
    filament::Scene& target;
    std::unique_ptr<utils::NameComponentManager> names;
    struct provider_deleter {
        void operator()(filament::gltfio::MaterialProvider* provider) const {
            provider->destroyMaterials();
            delete provider;
        }
    };
    struct loader_deleter {
        void operator()(filament::gltfio::AssetLoader* loader) const {
            filament::gltfio::AssetLoader::destroy(&loader);
        }
    };
    std::unique_ptr<filament::gltfio::MaterialProvider, provider_deleter> provider;
    std::unique_ptr<filament::gltfio::TextureProvider> textures;
    std::unique_ptr<filament::gltfio::AssetLoader, loader_deleter> loader;
    struct asset_record {
        filament::gltfio::FilamentAsset* asset{};
        std::vector<scene_node> nodes;
        bool instanced{};
    };
    struct mesh_record {
        filament::VertexBuffer* vertices{};
        filament::IndexBuffer* indices{};
        filament::MorphTargetBuffer* morphs{};
        bounds volume;
    };
    struct material_record {
        filament::MaterialInstance* material{};
        bool owned{};
    };
    std::vector<asset_record> assets;
    std::vector<filament::gltfio::FilamentInstance*> instances;
    std::vector<mesh_record> meshes;
    std::vector<filament::Material*> shaders;
    std::vector<material_record> materials;
    std::vector<utils::Entity> owned_nodes;
    std::unique_ptr<filament_environment> environment;

  public:
    explicit impl(renderer& graphics);
    ~impl();
};
scene::impl& scene_data(scene&);
}
