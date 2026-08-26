#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in float intensity;

layout(std140, binding = 0) uniform FrameData {
    float pointSize;
};

layout(std430, binding = 1) buffer ChunkData {
    vec4 relativeChunkOrigin;
};

layout(location = 2) out float vertexIntensity;
out vec4 vertexColor;

void main() {
    vec3 localPosition = position + relativeChunkOrigin.xyz;
    gl_Position = vec4(localPosition, 1.0);
    gl_PointSize = pointSize;
    vertexColor = color;
    vertexIntensity = intensity;
}
