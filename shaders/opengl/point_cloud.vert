#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in float intensity;

layout(std140, binding = 0) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec4 fixedColor;
    vec4 heightRange;
    vec4 intensityRange;
    float pointSize;
    uint shadingMode;
    vec2 reservedPadding;
    vec4 reservedExtension;
};

layout(std430, binding = 1) readonly buffer ChunkData {
    vec4 relativeChunkOrigin[];
};

layout(location = 2) out float vertexIntensity;
out vec4 vertexColor;

vec4 layoutAnchor() {
    if (shadingMode == 0xffffffffu) {
        return fixedColor + heightRange + intensityRange
            + vec4(reservedPadding, 0.0, 0.0) + reservedExtension;
    }
    return vec4(0.0);
}

void main() {
    vec3 localPosition = position + relativeChunkOrigin[0].xyz;
    gl_Position = projection * view * vec4(localPosition, 1.0);
    gl_PointSize = pointSize;
    vertexColor = color + layoutAnchor();
    vertexIntensity = intensity;
}
