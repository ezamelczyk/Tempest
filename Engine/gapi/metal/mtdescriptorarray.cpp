#if defined(TEMPEST_BUILD_METAL)

#include "mtdescriptorarray.h"

#include "mtdevice.h"
#include "mttexture.h"
#include "mtbuffer.h"
#include "mtpipelinelay.h"
#include "mtaccelerationstructure.h"
#include "mtshader.h"

using namespace Tempest;
using namespace Tempest::Detail;

MtDescriptorArray::MtDescriptorArray(MtDevice& dev, const MtPipelineLay &lay)
  :dev(dev), lay(&lay), desc(lay.lay.size()), bufferSizeBuffer(lay.bufferSizeBuffer) {
  }

void MtDescriptorArray::set(size_t id, AbstractGraphicsApi::Texture *tex, const Sampler &smp, uint32_t mipLevel) {
  auto& t = *reinterpret_cast<MtTexture*>(tex);
  desc[id].val     = &t.view(smp.mapping,mipLevel);
  desc[id].sampler = &dev.samplers.get(smp);
  }

void MtDescriptorArray::set(size_t id, const Sampler& smp) {
  desc[id].val     = nullptr;
  desc[id].sampler = &dev.samplers.get(smp);
  }

void MtDescriptorArray::set(size_t id, AbstractGraphicsApi::Buffer *buf, size_t offset) {
  auto& b = *reinterpret_cast<MtBuffer*>(buf);
  desc[id].val    = b.impl.get();
  desc[id].offset = offset;
  }

void MtDescriptorArray::setTlas(size_t id, AbstractGraphicsApi::AccelerationStructure* a) {
  auto& as = *reinterpret_cast<MtTopAccelerationStructure*>(a);
  desc[id].val  = as.impl.get();
  desc[id].tlas = &as;
  }

void MtDescriptorArray::set(size_t id, AbstractGraphicsApi::Texture** tex, size_t cnt,
                            const Sampler& smp, uint32_t mipLevel) {
  auto& d = desc[id];
  d.argsBuf = MtBuffer(dev, nullptr, cnt,
                       sizeof(MTL::ResourceID), sizeof(MTL::ResourceID),
                       MTL::ResourceStorageModePrivate);

  std::unique_ptr<MTL::ResourceID[]> addr(new MTL::ResourceID[cnt]);
  d.args.resize(cnt);
  for(size_t i=0; i<cnt; ++i) {
    addr[i]   = reinterpret_cast<MtTexture*>(tex[i])->impl->gpuResourceID();
    d.args[i] = reinterpret_cast<MtTexture*>(tex[i])->impl.get();
    }
  d.argsBuf.update(addr.get(), 0, cnt, sizeof(MTL::ResourceID), sizeof(MTL::ResourceID));
  }

void MtDescriptorArray::fillBufferSizeBuffer(uint32_t* ret, ShaderReflection::Stage stage) {
  auto& lx  = lay.handler->lay;
  auto& mtl = lay.handler->bind;
  for(size_t i=0; i<lx.size(); ++i) {
    if(lx[i].cls==ShaderReflection::SsboR ||
       lx[i].cls==ShaderReflection::SsboRW) {
      uint32_t at = uint32_t(-1);
      switch(stage) {
        case ShaderReflection::None:
        case ShaderReflection::Control:
        case ShaderReflection::Evaluate:
        case ShaderReflection::Geometry:
          break;
        case ShaderReflection::Task:
          at = mtl[i].bindTs;
          break;
        case ShaderReflection::Mesh:
          at = mtl[i].bindMs;
          break;
        case ShaderReflection::Vertex:
          at = mtl[i].bindVs;
          break;
        case ShaderReflection::Fragment:
          at = mtl[i].bindFs;
          break;
        case ShaderReflection::Compute:
          at = mtl[i].bindCs;
          break;
        }
      if(at==uint32_t(-1))
        continue;
      auto& b = *reinterpret_cast<MTL::Buffer*>(desc[i].val);
      ret[at] = b.length() - desc[i].offset;
      }
    }
  }

void MtDescriptorArray::useResource(MTL::ComputeCommandEncoder& cmd) {
  implUseResource(cmd);
  }

void MtDescriptorArray::useResource(MTL::RenderCommandEncoder& cmd) {
  implUseResource(cmd);
  }

template<class Enc>
void MtDescriptorArray::implUseResource(Enc&  cmd) {
  auto& lx  = lay.handler->lay;
  auto& mtl = lay.handler->bind;
  for(size_t i=0; i<lx.size(); ++i) {
    if(lx[i].cls==ShaderReflection::Tlas) {
      auto& blas = desc[i].tlas->blas;
      cmd.useResources(blas.data(), blas.size(), MTL::ResourceUsageRead);
      }
    if(lx[i].cls==ShaderReflection::Image && lx[i].runtimeSized) {
      auto& args = desc[i].args;
      cmd.useResources(args.data(), args.size(), MTL::ResourceUsageRead);
      }
    }
  }

#endif
