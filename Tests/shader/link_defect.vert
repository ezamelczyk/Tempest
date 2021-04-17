#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
  vec4 gl_Position;
  };

layout(location = 0) in vec2 inPos;

layout(location = 0) out VsData {
  vec4 array[2]; // spirv-cross bug: arrays are not mapped properly
  vec2 pos;
  } shOut;

/* directx12 bug: code generated by spirv-cross is valid, but triggers dx12 bug:

D3D12 ERROR: ID3D12Device::CreateGraphicsPipelineState: Vertex Shader - Pixel Shader linkage error:
Signatures between stages are incompatible. Semantic 'TEXCOORD' is defined for mismatched hardware
registers between the output stage and input stage.
[ STATE_CREATION ERROR #660: CREATEGRAPHICSPIPELINESTATE_SHADER_LINKAGE_REGISTERINDEX]
*/
void main() {
  shOut.pos   = inPos;
  gl_Position = vec4(inPos, 1.0, 1.0);
  }