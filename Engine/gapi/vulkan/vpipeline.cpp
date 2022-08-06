#if defined(TEMPEST_BUILD_VULKAN)

#include "vpipeline.h"

#include "vdevice.h"
#include "vshader.h"
#include "vpipelinelay.h"
#include "vmeshshaderemulated.h"
#include "vmeshlethelper.h"

#include <algorithm>

#include <Tempest/PipelineLayout>
#include <Tempest/RenderState>

using namespace Tempest;
using namespace Tempest::Detail;

bool VPipeline::InstDr::isCompatible(const VkPipelineRenderingCreateInfoKHR& dr, size_t stride) const {
  if(this->stride!=stride)
    return false;
  if(lay.viewMask!=dr.viewMask)
    return false;
  if(lay.colorAttachmentCount!=dr.colorAttachmentCount)
    return false;
  if(std::memcmp(lay.pColorAttachmentFormats,dr.pColorAttachmentFormats,lay.colorAttachmentCount*sizeof(VkFormat))!=0)
    return false;
  if(lay.depthAttachmentFormat!=dr.depthAttachmentFormat ||
     lay.stencilAttachmentFormat!=dr.stencilAttachmentFormat)
    return false;
  return true;
  }


VPipeline::VPipeline(){
  }

VPipeline::VPipeline(VDevice& device, const RenderState& st, Topology tp,
                     const VPipelineLay& ulay,
                     const VShader** sh, size_t count)
  : device(device.device.impl), st(st), tp(tp)  {
  try {
    for(size_t i=0; i<count; ++i)
      if(sh[i]!=nullptr)
        modules[i] = Detail::DSharedPtr<const VShader*>{sh[i]};

    if(auto vert=findShader(ShaderReflection::Stage::Vertex)) {
      declSize = vert->vdecl.size();
      decl.reset(new Decl::ComponentType[declSize]);
      std::memcpy(decl.get(),vert->vdecl.data(),declSize*sizeof(Decl::ComponentType));
      defaultStride = 0;
      for(size_t i=0;i<declSize;++i){
        defaultStride += uint32_t(Decl::size(decl[i]));
        }
      }
    pipelineLayout = initLayout(device,ulay,pushStageFlags,pushSize,false);

    if(auto ms=findShader(ShaderReflection::Stage::Mesh)) {
      device.allocMeshletHelper();

      pipelineLayoutMs = initLayout(device,ulay,pushStageFlags,pushSize,true);

      VkComputePipelineCreateInfo info = {};
      info.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
      info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
      info.stage.module = reinterpret_cast<const VMeshShaderEmulated*>(ms)->compPass;
      info.stage.pName  = "main";
      info.layout       = pipelineLayoutMs;
      vkAssert(vkCreateComputePipelines(device.device.impl, VK_NULL_HANDLE, 1, &info, nullptr, &meshCompuePipeline));
      }
    }
  catch(...) {
    cleanup();
    throw;
    }
  }

VPipeline::~VPipeline() {
  cleanup();
  }

VkPipeline VPipeline::instance(const std::shared_ptr<VFramebufferMap::RenderPass>& pass, size_t stride) {
  std::lock_guard<SpinLock> guard(sync);

  for(auto& i:instRp)
    if(i.stride!=stride || i.lay->isCompatible(*pass))
      return i.val;
  VkPipeline val=VK_NULL_HANDLE;
  try {
    val = initGraphicsPipeline(device,pipelineLayout,pass.get(),nullptr,st,
                               decl.get(),declSize,stride,
                               tp,modules);
    instRp.emplace_back(pass,stride,val);
    }
  catch(...) {
    if(val!=VK_NULL_HANDLE)
      vkDestroyPipeline(device,val,nullptr);
    throw;
    }
  return instRp.back().val;
  }

VkPipeline VPipeline::instance(const VkPipelineRenderingCreateInfoKHR& info, size_t stride) {
  std::lock_guard<SpinLock> guard(sync);

  for(auto& i:instDr)
    if(i.isCompatible(info,stride))
      return i.val;
  VkPipeline val=VK_NULL_HANDLE;
  try {
    val = initGraphicsPipeline(device,pipelineLayout,nullptr,&info,st,
                               decl.get(),declSize,stride,
                               tp,modules);
    instDr.emplace_back(info,stride,val);
    }
  catch(...) {
    if(val!=VK_NULL_HANDLE)
      vkDestroyPipeline(device,val,nullptr);
    throw;
    }
  return instDr.back().val;
  }

VkPipeline VPipeline::meshPipeline() const {
  return meshCompuePipeline;
  }

VkPipelineLayout VPipeline::meshPipelineLayout() const {
  return pipelineLayoutMs;
  }

const VShader* VPipeline::findShader(ShaderReflection::Stage sh) const {
  for(auto& i:modules)
    if(i.handler!=nullptr && i.handler->stage==sh) {
      return i.handler;
      }
  return nullptr;
  }

void VPipeline::cleanup() {
  if(pipelineLayoutMs!=VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device,pipelineLayoutMs,nullptr);
  if(meshCompuePipeline!=VK_NULL_HANDLE)
    vkDestroyPipeline(device,meshCompuePipeline,nullptr);

  if(pipelineLayout!=VK_NULL_HANDLE)
    vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
  for(auto& i:instRp)
    vkDestroyPipeline(device,i.val,nullptr);
  for(auto& i:instDr)
    vkDestroyPipeline(device,i.val,nullptr);
  }

VkPipelineLayout VPipeline::initLayout(VDevice& device, const VPipelineLay& uboLay,
                                       VkShaderStageFlags& pushStageFlags, uint32_t& pushSize,
                                       bool isMeshCompPass) {
  VkPushConstantRange push = {};

  VkDescriptorSetLayout pSetLayouts[4] = {uboLay.impl};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
  pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.pSetLayouts            = pSetLayouts;
  pipelineLayoutInfo.setLayoutCount         = 1;
  pipelineLayoutInfo.pushConstantRangeCount = 0;

  if(uboLay.msHelper!=VK_NULL_HANDLE) {
    if(isMeshCompPass) {
      auto& ms = *device.meshHelper.get();
      pSetLayouts[pipelineLayoutInfo.setLayoutCount] = ms.lay();
      pipelineLayoutInfo.setLayoutCount++;
      } else {
      pSetLayouts[pipelineLayoutInfo.setLayoutCount] = uboLay.msHelper;
      pipelineLayoutInfo.setLayoutCount++;
      }
    }

  if(uboLay.pb.size>0) {
    pushStageFlags = nativeFormat(uboLay.pb.stage);
    push.stageFlags = pushStageFlags;
    push.offset     = 0;
    push.size       = uint32_t(uboLay.pb.size);

    pushSize        = push.size;

    pipelineLayoutInfo.pPushConstantRanges    = &push;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    }

  VkPipelineLayout ret;
  vkAssert(vkCreatePipelineLayout(device.device.impl,&pipelineLayoutInfo,nullptr,&ret));
  return ret;
  }

VkPipeline VPipeline::initGraphicsPipeline(VkDevice device, VkPipelineLayout layout,
                                           const VFramebufferMap::RenderPass* rpLay,
                                           const VkPipelineRenderingCreateInfoKHR* dynLay,
                                           const RenderState &st,
                                           const Decl::ComponentType *decl, size_t declSize,
                                           size_t stride, Topology tp,
                                           const DSharedPtr<const VShader*>* shaders) {
  VkPipelineShaderStageCreateInfo shaderStages[5] = {};
  size_t                          stagesCnt       = 0;
  for(size_t i=0; i<5; ++i) {
    if(shaders[i].handler!=nullptr) {
      VkPipelineShaderStageCreateInfo& sh = shaderStages[stagesCnt];
      sh.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      sh.stage  = nativeFormat(shaders[i].handler->stage);
      sh.module = shaders[i].handler->impl;
      sh.pName  = "main";
      if(auto ms = dynamic_cast<const VMeshShaderEmulated*>(shaders[i].handler)) {
        sh.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        sh.module = ms->impl;
        }
      stagesCnt++;
      }
    }

  const bool useTesselation = (findShader(ShaderReflection::Stage::Evaluate)!=nullptr ||
                               findShader(ShaderReflection::Stage::Control) !=nullptr);

  VkVertexInputBindingDescription vertexInputBindingDescription;
  vertexInputBindingDescription.binding   = 0;
  vertexInputBindingDescription.stride    = uint32_t(stride);
  vertexInputBindingDescription.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

  SmallArray<VkVertexInputAttributeDescription,16> vsInput(declSize);
  uint32_t offset=0;
  for(size_t i=0;i<declSize;++i){
    auto& loc=vsInput[i];
    loc.location = uint32_t(i);
    loc.binding  = 0;
    loc.format   = nativeFormat(decl[i]);
    loc.offset   = offset;

    offset += uint32_t(Decl::size(decl[i]));
    }

  VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
  vertexInputInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.pNext = nullptr;
  vertexInputInfo.flags = 0;
  vertexInputInfo.vertexBindingDescriptionCount   = (declSize>0 ? 1 : 0);
  vertexInputInfo.pVertexBindingDescriptions      = &vertexInputBindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount = uint32_t(declSize);
  vertexInputInfo.pVertexAttributeDescriptions    = vsInput.get();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
  inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.primitiveRestartEnable = VK_FALSE;
  inputAssembly.topology               = nativeFormat(tp);

  if(findShader(ShaderReflection::Stage::Vertex)!=nullptr) {
    if(useTesselation)
      inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    } else {
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    }

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports    = nullptr;
  viewportState.scissorCount  = 1;
  viewportState.pScissors     = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterizer = {};
  rasterizer.sType                   = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.pNext                   = nullptr;
  rasterizer.flags                   = 0;
  rasterizer.rasterizerDiscardEnable = st.isRasterDiscardEnabled() ? VK_TRUE : VK_FALSE;
  rasterizer.polygonMode             = VkPolygonMode::VK_POLYGON_MODE_FILL;
  rasterizer.cullMode                = nativeFormat(st.cullFaceMode());
  rasterizer.frontFace               = VkFrontFace::VK_FRONT_FACE_CLOCKWISE;
  rasterizer.depthClampEnable        = VK_FALSE;
  rasterizer.depthBiasEnable         = VK_FALSE;
  rasterizer.depthBiasConstantFactor = 0.0f;
  rasterizer.depthBiasClamp          = 0.0f;
  rasterizer.depthBiasSlopeFactor    = 0.0f;
  rasterizer.lineWidth               = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampling = {};
  multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable  = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blendAtt[MaxFramebufferAttachments] = {};
  uint32_t                            blendAttCount = 0;
  {
  const size_t size = rpLay!=nullptr ? rpLay->descSize : dynLay->colorAttachmentCount;
  for(size_t i=0; i<size; ++i) {
    const VkFormat frm = rpLay!=nullptr ? rpLay->desc[i].frm : dynLay->pColorAttachmentFormats[i];
    if(nativeIsDepthFormat(frm))
      continue;
    auto& a = blendAtt[i];
    a.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    a.blendEnable         = st.hasBlend() ? VK_TRUE : VK_FALSE;
    a.dstColorBlendFactor = nativeFormat(st.blendDest());
    a.srcColorBlendFactor = nativeFormat(st.blendSource());
    a.colorBlendOp        = nativeFormat(st.blendOperation());
    a.dstAlphaBlendFactor = a.dstColorBlendFactor;
    a.srcAlphaBlendFactor = a.srcColorBlendFactor;
    a.alphaBlendOp        = a.colorBlendOp;
    ++blendAttCount;
    }
  }

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable     = VK_FALSE;
  colorBlending.logicOp           = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount   = blendAttCount;
  colorBlending.pAttachments      = blendAtt;
  colorBlending.blendConstants[0] = 0.0f;
  colorBlending.blendConstants[1] = 0.0f;
  colorBlending.blendConstants[2] = 0.0f;
  colorBlending.blendConstants[3] = 0.0f;

  VkPipelineDepthStencilStateCreateInfo depthStencil = {};
  depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable       = st.zTestMode()!=RenderState::ZTestMode::Always ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable      = st.isZWriteEnabled() ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp        = nativeFormat(st.zTestMode());
  depthStencil.depthBoundsTestEnable = VK_FALSE;
  depthStencil.stencilTestEnable     = VK_FALSE;

  VkPipelineTessellationStateCreateInfo tesselation = {};
  tesselation.sType                 = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
  tesselation.flags                 = 0;
  if(tp==Triangles)
    tesselation.patchControlPoints = 3; else
    tesselation.patchControlPoints = 2;

  VkPipelineDynamicStateCreateInfo dynamic = {};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  const VkDynamicState dySt[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  dynamic.pDynamicStates    = dySt;
  dynamic.dynamicStateCount = 2;

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount          = uint32_t(stagesCnt);
  pipelineInfo.pStages             = shaderStages;
  pipelineInfo.pVertexInputState   = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState      = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState   = &multisampling;
  pipelineInfo.pDepthStencilState  = &depthStencil;
  pipelineInfo.pColorBlendState    = &colorBlending;
  pipelineInfo.pDynamicState       = &dynamic;
  pipelineInfo.layout              = layout;
  pipelineInfo.subpass             = 0;
  pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;

  if(rpLay!=nullptr)
    pipelineInfo.renderPass = rpLay->pass; else
    pipelineInfo.pNext      = dynLay;

  if(useTesselation) {
    pipelineInfo.pTessellationState = &tesselation;
    // rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    }

  VkPipeline graphicsPipeline=VK_NULL_HANDLE;
  vkAssert(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipelineInfo,nullptr,&graphicsPipeline));
  return graphicsPipeline;
  }


VCompPipeline::VCompPipeline() {
  }

VCompPipeline::VCompPipeline(VDevice& dev, const VPipelineLay& ulay, const VShader& comp)
  :device(dev.device.impl) {
  VkShaderStageFlags pushStageFlags = 0;
  pipelineLayout = VPipeline::initLayout(dev,ulay,pushStageFlags,pushSize,false);
  wgSize         = comp.comp.wgSize;

  try {
    VkComputePipelineCreateInfo info = {};
    info.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = comp.impl;
    info.stage.pName  = "main";
    info.layout       = pipelineLayout;
    vkAssert(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &impl));
    }
  catch(...) {
    vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
    throw;
    }
  }

VCompPipeline::~VCompPipeline() {
  if(pipelineLayout==VK_NULL_HANDLE)
    return;
  vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
  vkDestroyPipeline(device,impl,nullptr);
  }

IVec3 VCompPipeline::workGroupSize() const {
  return wgSize;
  }

#endif
