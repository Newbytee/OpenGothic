#pragma once

#include <Tempest/RenderPipeline>
#include <Tempest/RenderPass>
#include <Tempest/CommandBuffer>
#include <Tempest/Matrix4x4>
#include <Tempest/Widget>
#include <Tempest/Device>
#include <Tempest/UniformBuffer>
#include <Tempest/VectorImage>

#include "worldview.h"
#include "rendererstorage.h"

class Gothic;
class Camera;
class InventoryMenu;

class Renderer final {
  public:
    Renderer(Tempest::Device& device, Tempest::Swapchain& swapchain, Gothic &gothic);

    void resetSwapchain();
    void onWorldChanged();

    void setCameraView(const Camera &camera);

    void draw(Tempest::Encoder<Tempest::CommandBuffer>& cmd, uint8_t frameId, uint8_t imgId,
              Tempest::VectorImage& uiLayer, Tempest::VectorImage& numOverlay,
              InventoryMenu &inventory, const Gothic& gothic);

    Tempest::Attachment               screenshoot(uint8_t frameId);
    const RendererStorage&            storage() const { return stor; }

  private:
    Tempest::Device&                  device;
    Tempest::Swapchain&               swapchain;
    Gothic&                           gothic;
    Tempest::Matrix4x4                view, viewProj;
    Tempest::Matrix4x4                shadow[Resources::ShadowLayers];

    Tempest::Attachment               shadowMap[Resources::ShadowLayers];
    Tempest::ZBuffer                  zbuffer, zbufferItem, shadowZ[Resources::ShadowLayers];

    Tempest::Attachment               lightingBuf;
    Tempest::Attachment               gbufDiffuse;
    Tempest::Attachment               gbufNormal;
    Tempest::Attachment               gbufDepth;

    Tempest::TextureFormat            shadowFormat =Tempest::TextureFormat::RGBA8;
    Tempest::TextureFormat            zBufferFormat=Tempest::TextureFormat::Depth16;

    std::vector<Tempest::FrameBuffer> fbo3d, fboCpy, fboUi, fboItem;
    Tempest::FrameBuffer              fboShadow[2], fboGBuf;

    Tempest::RenderPass               mainPass, mainPassNoGbuf, gbufPass, shadowPass, copyPass;
    Tempest::RenderPass               inventoryPass;
    Tempest::RenderPass               uiPass;

    Tempest::Uniforms                 uboCopy;
    RendererStorage                   stor;

    void draw(Tempest::Encoder<Tempest::CommandBuffer> &cmd, Tempest::FrameBuffer& fbo, Tempest::FrameBuffer& fboCpy, const Gothic& gothic, uint8_t frameId);
    void draw(Tempest::Encoder<Tempest::CommandBuffer> &cmd, Tempest::FrameBuffer& fbo, InventoryMenu& inv);
    void draw(Tempest::Encoder<Tempest::CommandBuffer> &cmd, Tempest::FrameBuffer& fbo, Tempest::VectorImage& surface);
  };
