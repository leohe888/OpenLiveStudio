#version 330 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 textureCoordinate;

uniform mat4 vertexTransform;
uniform bool flipY;
out vec2 uv;

void main()
{
    gl_Position = vertexTransform * vec4(position, 0.0, 1.0);
    uv = vec2(textureCoordinate.x, flipY ? 1.0 - textureCoordinate.y : textureCoordinate.y);
}
