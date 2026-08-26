#version 450 core

layout(location = 2) in float vertexIntensity;
in vec4 vertexColor;
out vec4 fragmentColor;

void main() {
    fragmentColor = vertexColor;
    // Intensity is part of the fixed interface; color mapping is deferred to GL-009.
}
