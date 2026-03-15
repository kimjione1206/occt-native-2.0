#pragma once

// ─── Embedded GLSL shader sources for Vulkan stress testing ──────────────────
// These are compiled to SPIR-V at build time via glslangValidator or glslc.
// If no SPIR-V compiler is available, pre-compiled SPIR-V arrays are used.

namespace occt { namespace gpu { namespace shaders {

// ─── Vertex shader ──────────────────────────────────────────────────────────
// Transforms vertices, computes normals, passes data to fragment shader.

static const char* const kStressVertGLSL = R"GLSL(
#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    vec4 params;  // x=time, y=complexity, z=draw_index, w=total_draws
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragTime;
layout(location = 3) out float fragComplexity;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    fragPos = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal;
    fragTime = pc.params.x;
    fragComplexity = pc.params.y;
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
)GLSL";

// ─── Fragment shader Level 1: Basic Phong ───────────────────────────────────

static const char* const kStressL1FragGLSL = R"GLSL(
#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragTime;
layout(location = 3) in float fragComplexity;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPos);

    // Ambient
    vec3 ambient = vec3(0.1, 0.1, 0.15);

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * vec3(0.6, 0.3, 0.2);

    // Specular (Phong)
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = spec * vec3(1.0, 1.0, 1.0);

    outColor = vec4(ambient + diffuse + specular, 1.0);
}
)GLSL";

// ─── Fragment shader Level 2: + Normal mapping + Enhanced specular ──────────

static const char* const kStressL2FragGLSL = R"GLSL(
#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragTime;
layout(location = 3) in float fragComplexity;

layout(location = 0) out vec4 outColor;

vec3 proceduralNormalMap(vec3 pos) {
    float scale = 10.0;
    float nx = sin(pos.x * scale) * cos(pos.z * scale);
    float ny = cos(pos.y * scale) * sin(pos.x * scale);
    float nz = sin(pos.z * scale) * cos(pos.y * scale);
    return normalize(vec3(nx, ny, nz) * 0.3 + normalize(fragNormal) * 0.7);
}

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 lightDir2 = normalize(vec3(-1.0, 0.5, -0.5));
    vec3 normal = proceduralNormalMap(fragPos);
    vec3 viewDir = normalize(-fragPos);

    // Ambient
    vec3 ambient = vec3(0.08, 0.08, 0.12);

    // Two-light diffuse
    float diff1 = max(dot(normal, lightDir), 0.0);
    float diff2 = max(dot(normal, lightDir2), 0.0);
    vec3 diffuse = diff1 * vec3(0.7, 0.3, 0.2) + diff2 * vec3(0.2, 0.3, 0.7) * 0.5;

    // Blinn-Phong specular
    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfway), 0.0), 64.0);
    vec3 specular = spec * vec3(1.0, 0.9, 0.8) * 0.8;

    // Fresnel rim
    float fresnel = pow(1.0 - max(dot(viewDir, normal), 0.0), 3.0);
    vec3 rim = fresnel * vec3(0.3, 0.4, 0.6);

    outColor = vec4(ambient + diffuse + specular + rim, 1.0);
}
)GLSL";

// ─── Fragment shader Level 3: + Complex math (procedural texturing) ─────────

static const char* const kStressL3FragGLSL = R"GLSL(
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

    // Procedural marble texture using FBM
    float marble = fbm(fragPos * 3.0 + vec3(fragTime * 0.1), 6);
    marble = sin(fragPos.x * 5.0 + marble * 10.0) * 0.5 + 0.5;

    vec3 baseColor = mix(vec3(0.8, 0.75, 0.7), vec3(0.3, 0.25, 0.2), marble);

    // Multiple lights with attenuation
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

    // Ambient occlusion approximation
    float ao = 1.0 - fbm(fragPos * 5.0, 4) * 0.3;
    color *= ao;

    outColor = vec4(color, 1.0);
}
)GLSL";

// ─── Fragment shader Level 4: + Volumetric effects ──────────────────────────

static const char* const kStressL4FragGLSL = R"GLSL(
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
            // Light march
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

    // Surface shading
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfway), 0.0), 64.0);

    float marble = sin(fragPos.x * 5.0 + fbm(fragPos * 3.0, 6) * 10.0) * 0.5 + 0.5;
    vec3 baseColor = mix(vec3(0.7, 0.4, 0.2), vec3(0.2, 0.4, 0.7), marble);

    vec3 surfaceColor = baseColor * (vec3(0.1) + diff * vec3(0.8)) + spec * vec3(0.5);

    // Volumetric contribution
    vec3 volColor = volumetricLighting(fragPos, -viewDir, 4.0);

    // Subsurface scattering approximation
    float sss = pow(max(dot(viewDir, -lightDir), 0.0), 4.0);
    vec3 sssColor = sss * baseColor * 0.3;

    outColor = vec4(surfaceColor + volColor * 0.5 + sssColor, 1.0);
}
)GLSL";

// ─── Fragment shader Level 5: + Ray marching ────────────────────────────────

static const char* const kStressL5FragGLSL = R"GLSL(
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

// SDF for a sphere
float sdSphere(vec3 p, float r) { return length(p) - r; }

// SDF for a torus
float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

// SDF for a fractal-like object
float sdFractal(vec3 p) {
    float d = sdSphere(p, 1.0);
    float s = 1.0;
    for (int i = 0; i < 6; i++) {
        vec3 a = mod(p * s, 2.0) - 1.0;
        s *= 3.0;
        vec3 r = abs(1.0 - 3.0 * abs(a));
        float da = max(r.x, r.y);
        float db = max(r.y, r.z);
        float dc = max(r.z, r.x);
        float c = (min(da, min(db, dc)) - 1.0) / s;
        d = max(d, c);
    }
    return d;
}

// Scene SDF
float sceneSDF(vec3 p) {
    float t = fragTime;
    // Animated fractal
    vec3 rp = p;
    float ca = cos(t * 0.3), sa = sin(t * 0.3);
    rp.xz = mat2(ca, sa, -sa, ca) * rp.xz;

    float fractal = sdFractal(rp);
    float torus = sdTorus(p - vec3(0, sin(t * 0.5) * 0.5, 0), vec2(1.5, 0.3));

    float d = min(fractal, torus);

    // Add noise displacement
    d += fbm(p * 3.0 + t * 0.1, 4) * 0.05;

    return d;
}

vec3 calcNormal(vec3 p) {
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        sceneSDF(p + e.xyy) - sceneSDF(p - e.xyy),
        sceneSDF(p + e.yxy) - sceneSDF(p - e.yxy),
        sceneSDF(p + e.yyx) - sceneSDF(p - e.yyx)
    ));
}

float calcAO(vec3 pos, vec3 nor) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.01 + 0.12 * float(i);
        float d = sceneSDF(pos + h * nor);
        occ += (h - d) * sca;
        sca *= 0.95;
    }
    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

float softShadow(vec3 ro, vec3 rd, float mint, float maxt) {
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 24 && t < maxt; i++) {
        float h = sceneSDF(ro + rd * t);
        res = min(res, 8.0 * h / t);
        t += clamp(h, 0.02, 0.2);
        if (h < 0.001) break;
    }
    return clamp(res, 0.0, 1.0);
}

void main() {
    vec3 viewDir = normalize(-fragPos);

    // Ray march from fragment position
    vec3 ro = fragPos;
    vec3 rd = -viewDir;

    float t = 0.0;
    float d = 0.0;
    vec3 p = ro;
    bool hit = false;

    for (int i = 0; i < 128; i++) {
        p = ro + rd * t;
        d = sceneSDF(p);
        if (d < 0.001) { hit = true; break; }
        if (t > 20.0) break;
        t += d;
    }

    vec3 color;
    if (hit) {
        vec3 n = calcNormal(p);
        vec3 lightDir = normalize(vec3(1, 2, 1));

        // Material
        float marble = sin(p.x * 5.0 + fbm(p * 4.0, 5) * 8.0) * 0.5 + 0.5;
        vec3 baseColor = mix(vec3(0.9, 0.3, 0.1), vec3(0.1, 0.5, 0.9), marble);

        // Lighting
        float diff = max(dot(n, lightDir), 0.0);
        float shadow = softShadow(p + n * 0.01, lightDir, 0.02, 5.0);
        float ao = calcAO(p, n);

        vec3 halfway = normalize(lightDir + viewDir);
        float spec = pow(max(dot(n, halfway), 0.0), 128.0);

        // Fresnel
        float fresnel = pow(1.0 - max(dot(viewDir, n), 0.0), 5.0);

        color = baseColor * (vec3(0.05) * ao + diff * shadow * vec3(1.0, 0.9, 0.8));
        color += spec * shadow * vec3(1.0);
        color += fresnel * vec3(0.2, 0.3, 0.5) * ao;

        // Fog
        float fog = 1.0 - exp(-t * 0.1);
        color = mix(color, vec3(0.05, 0.05, 0.1), fog);
    } else {
        // Background: procedural sky
        float skyGrad = rd.y * 0.5 + 0.5;
        color = mix(vec3(0.05, 0.05, 0.1), vec3(0.1, 0.15, 0.3), skyGrad);

        // Stars
        float starNoise = fbm(rd * 100.0, 4);
        if (starNoise > 0.85) {
            color += vec3(pow(starNoise - 0.85, 2.0) * 50.0);
        }
    }

    // Tone mapping
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
)GLSL";

// ─── Array of all fragment shader sources indexed by level (0-4) ────────────

static const char* const kStressFragGLSL[] = {
    kStressL1FragGLSL,
    kStressL2FragGLSL,
    kStressL3FragGLSL,
    kStressL4FragGLSL,
    kStressL5FragGLSL,
};

static constexpr int kMaxShaderLevel = 5;

}}} // namespace occt::gpu::shaders
