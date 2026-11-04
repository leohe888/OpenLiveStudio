#version 330 core

in vec2 texCoord;
out vec4 result;

uniform sampler2D plane0, plane1, plane2;
uniform int layoutType;
uniform bool fullRange, bt709;
uniform float imageWidth;

void main()
{
    // 输出纹理保持 CPU 图片的行序，以复用现有合成和滤镜的纹理方向。
    vec2 uv = vec2(texCoord.x, 1.0 - texCoord.y);
    vec4 p = texture(plane0, uv);
    if (layoutType >= 4) {
        result = vec4((layoutType == 5 || layoutType == 7) ? p.bgr : p.rgb, 1.0);
        return;
    }
    float y = p.r, u, v;
    if (layoutType == 0) {
        u = texture(plane1, uv).r;
        v = texture(plane2, uv).r;
    } else if (layoutType == 1) {
        vec2 c = texture(plane1, uv).rg;
        u = c.r;
        v = c.g;
    } else {
        bool odd = (int(floor(uv.x * imageWidth)) & 1) != 0;
        if (layoutType == 2) {
            y = odd ? p.b : p.r;
            u = p.g;
            v = p.a;
        } else {
            y = odd ? p.a : p.g;
            u = p.r;
            v = p.b;
        }
    }
    vec2 c = vec2(u, v) - vec2(128.0 / 255.0);
    if (!fullRange) {
        y = (y - 16.0 / 255.0) * (255.0 / 219.0);
        c *= 255.0 / 224.0;
    }
    vec3 rgb;
    if (bt709) {
        rgb = vec3(y + 1.5748 * c.y,
                   y - 0.187324 * c.x - 0.468124 * c.y,
                   y + 1.8556 * c.x);
    } else {
        rgb = vec3(y + 1.402 * c.y,
                   y - 0.344136 * c.x - 0.714136 * c.y,
                   y + 1.772 * c.x);
    }
    result = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
