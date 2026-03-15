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
    float a = hash(i); float b = hash(i + vec3(1,0,0));
    float c = hash(i + vec3(0,1,0)); float d = hash(i + vec3(1,1,0));
    float e = hash(i + vec3(0,0,1)); float f2 = hash(i + vec3(1,0,1));
    float g = hash(i + vec3(0,1,1)); float h = hash(i + vec3(1,1,1));
    return mix(mix(mix(a,b,f.x), mix(c,d,f.x), f.y),
               mix(mix(e,f2,f.x), mix(g,h,f.x), f.y), f.z);
}

float fbm(vec3 p, int octaves) {
    float v = 0.0, a = 0.5, freq = 1.0;
    for (int i = 0; i < octaves; i++) {
        v += a * noise(p * freq); a *= 0.5; freq *= 2.0;
    }
    return v;
}

float volumetricDensity(vec3 p) {
    return fbm(p * 2.0 + vec3(fragTime * 0.15, fragTime * 0.1, fragTime * 0.05), 6);
}

vec3 volumetricLighting(vec3 ro, vec3 rd, float maxDist) {
    vec3 lightDir = normalize(vec3(1, 2, 1));
    vec3 lightColor = vec3(1.0, 0.8, 0.6);
    vec3 accumColor = vec3(0.0);
    float accumDensity = 0.0;
    float stepSize = maxDist / 32.0;

    for (int i = 0; i < 32; i++) {
        vec3 p = ro + rd * (float(i) * stepSize);
        float density = volumetricDensity(p);

        if (density > 0.01) {
            float lightDensity = 0.0;
            for (int j = 0; j < 8; j++) {
                vec3 lp = p + lightDir * float(j) * 0.2;
                lightDensity += volumetricDensity(lp) * 0.2;
            }

            float lightTransmittance = exp(-lightDensity * 2.0);
            vec3 sampleColor = lightColor * lightTransmittance * density;
            float sampleAlpha = density * stepSize;

            accumColor += sampleColor * (1.0 - accumDensity) * sampleAlpha;
            accumDensity += sampleAlpha * (1.0 - accumDensity);
        }

        if (accumDensity > 0.95) break;
    }

    return accumColor;
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPos);
    vec3 lightDir = normalize(vec3(1, 1, 1));

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfway), 0.0), 64.0);

    float marble = sin(fragPos.x * 5.0 + fbm(fragPos * 3.0, 6) * 10.0) * 0.5 + 0.5;
    vec3 baseColor = mix(vec3(0.7, 0.4, 0.2), vec3(0.2, 0.4, 0.7), marble);

    vec3 surfaceColor = baseColor * (vec3(0.1) + diff * vec3(0.8)) + spec * vec3(0.5);

    vec3 volColor = volumetricLighting(fragPos, -viewDir, 4.0);

    float sss = pow(max(dot(viewDir, -lightDir), 0.0), 4.0);
    vec3 sssColor = sss * baseColor * 0.3;

    outColor = vec4(surfaceColor + volColor * 0.5 + sssColor, 1.0);
}
