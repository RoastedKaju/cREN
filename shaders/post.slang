[[vk::binding(0, 0)]]
Texture2D textures2D[];
[[vk::binding(5, 0)]]
SamplerState samplers[];

struct PushConstants {
  uint64_t reserved; // the scene shader's vertex pointer lives here, so both shaders share one layout
  uint texture;
  uint sampler;
};
[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

struct VSOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vsMain(uint vertex_id: SV_VertexID) {
  // one oversized triangle covering the screen: (-1,-1) (3,-1) (-1,3)
  float2 p = float2((vertex_id << 1) & 2, vertex_id & 2);
  VSOutput o;
  o.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
  o.uv = p; // uv (0,0) is the top-left, matching Vulkan's Y-down framebuffer
  return o;
}

float3 aces(float3 x) {
  return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target {
  float3 hdr = textures2D[pc.texture].Sample(samplers[pc.sampler], input.uv).rgb;
  return float4(aces(hdr), 1.0); // the swapchain is sRGB, so the hardware applies gamma
}
