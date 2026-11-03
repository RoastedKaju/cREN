[[vk::binding(0, 0)]]
Texture2D textures2D[];
[[vk::binding(1, 0)]]
Texture2DArray textures2DArray[];
[[vk::binding(2, 0)]]
TextureCube texturesCube[];
[[vk::binding(3, 0)]]
TextureCubeArray texturesCubeArray[];
[[vk::binding(4, 0)]]
Texture3D textures3D[];
[[vk::binding(5, 0)]]
SamplerState samplers[];

struct Vertex {
  float3 position;
  float3 color;
};

struct PushConstants {
  Vertex *vertices;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

struct VSOutput {
  float4 position : SV_Position;
  float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vsMain(uint vertex_id: SV_VertexID) {
  Vertex v = pc.vertices[vertex_id];
  VSOutput output;
  output.position = float4(v.position, 1.0);
  output.color = v.color;
  return output;
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target {
  return float4(input.color, 1.0);
}
