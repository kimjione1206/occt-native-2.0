#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragTime;
layout(location = 3) in float fragComplexity;

layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec3(1,0,0));
    float c = hash(i + vec3(0,1,0));
    float d = hash(i + vec3(1,1,0));
    float e = hash(i + vec3(0,0,1));
    float f2 = hash(i + vec3(1,0,1));
    float g = hash(i + vec3(0,1,1));
    float h = hash(i + vec3(1,1,1));

    return mix(mix(mix(a,b,f.x), mix(c,d,f.x), f.y),
               mix(mix(e,f2,f.x), mix(g,h,f.x), f.y), f.z);
}

float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

void main() {
    vec3 lightDir = normalize(vec3(sin(fragTime * 0.5), 1.0, cos(fragTime * 0.5)));
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPos);

    float marble = fbm(fragPos * 3.0 + vec3(fragTime * 0.1), 6);
    marble = sin(fragPos.x * 5.0 + marble * 10.0) * 0.5 + 0.5;

    vec3 baseColor = mix(vec3(0.8, 0.75, 0.7), vec3(0.3, 0.25, 0.2), marble);

    vec3 color = vec3(0.05);
    for (int i = 0; i < 4; i++) {
        float angle = float(i) * 1.5708 + fragTime * 0.3;
        vec3 lp = vec3(sin(angle) * 3.0, 2.0, cos(angle) * 3.0);
        vec3 ld = normalize(lp - fragPos);
        float dist = length(lp - fragPos);
        float atten = 1.0 / (1.0 + 0.1 * dist + 0.01 * dist * dist);

        float diff = max(dot(normal, ld), 0.0);
        vec3 halfway = normalize(ld + viewDir);
        float spec = pow(max(dot(normal, halfway), 0.0), 128.0);

        vec3 lightColor = vec3(
            0.5 + 0.5 * sin(float(i) * 2.1),
            0.5 + 0.5 * sin(float(i) * 2.1 + 2.094),
            0.5 + 0.5 * sin(float(i) * 2.1 + 4.189)
        );

        color += atten * (diff * baseColor * lightColor + spec * vec3(1.0));
    }

    float ao = 1.0 - fbm(fragPos * 5.0, 4) * 0.3;
    color *= ao;

    outColor = vec4(color, 1.0);
}
