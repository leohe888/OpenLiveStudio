#version 330 core

in vec2 uv;
uniform sampler2D sourceTexture;
uniform bool colorCorrection;
uniform vec3 correction; // brightness, contrast, saturation
out vec4 fragmentColor;

void main()
{
    fragmentColor = texture(sourceTexture, uv);
    if (colorCorrection) {
        vec3 rgb = (fragmentColor.rgb - 0.5) * correction.y + 0.5 + correction.x;
        float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        fragmentColor.rgb = clamp(mix(vec3(luma), rgb, correction.z), 0.0, 1.0);
    }
}
