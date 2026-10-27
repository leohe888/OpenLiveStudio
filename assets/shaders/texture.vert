#version 330 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 textureCoordinate;

uniform vec2 displayScale;
uniform bool flipY;
out vec2 uv;

void main()
{
    gl_Position = vec4(position * displayScale, 0.0, 1.0);
    uv = vec2(textureCoordinate.x, flipY ? 1.0 - textureCoordinate.y : textureCoordinate.y);
}
