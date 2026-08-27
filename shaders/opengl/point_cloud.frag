#version 450 core

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

layout(location = 2) in float vertexIntensity;
in vec4 vertexColor;
in float vertexHeight;
out vec4 fragmentColor;

uniform uint drawHasColor;
uniform uint drawHasIntensity;

vec3 ramp(float value) {
    float v = clamp(value, 0.0, 1.0);
    if (v <= 0.25)
        return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), v / 0.25);
    if (v <= 0.5)
        return mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), (v - 0.25) / 0.25);
    if (v <= 0.75)
        return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (v - 0.5) / 0.25);
    return mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (v - 0.75) / 0.25);
}

float normalizeRange(float value, vec2 range) {
    if (range.x == range.y)
        return 0.5;
    return (value - range.x) / (range.y - range.x);
}

void main() {
    if (shadingMode == 0u)
        fragmentColor = drawHasColor != 0u ? vertexColor : fixedColor;
    else if (shadingMode == 1u)
        fragmentColor = fixedColor;
    else if (shadingMode == 2u)
        fragmentColor = vec4(ramp(normalizeRange(vertexHeight, heightRange.xy)), 1.0);
    else if (shadingMode == 3u)
        fragmentColor = drawHasIntensity != 0u
            ? vec4(ramp(normalizeRange(vertexIntensity, intensityRange.xy)), 1.0)
            : fixedColor;
    else
        fragmentColor = fixedColor;
}