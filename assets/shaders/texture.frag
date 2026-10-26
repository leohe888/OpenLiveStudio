#version 330 core

in vec2 uv;
uniform sampler2D sourceTexture;
out vec4 fragmentColor;

void main()
{
    fragmentColor = texture(sourceTexture, uv);
}
