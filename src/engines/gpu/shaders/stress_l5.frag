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

float sdSphere(vec3 p, float r) { return length(p) - r; }

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

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

float sceneSDF(vec3 p) {
    float t = fragTime;
    vec3 rp = p;
    float ca = cos(t * 0.3), sa = sin(t * 0.3);
    rp.xz = mat2(ca, sa, -sa, ca) * rp.xz;

    float fractal = sdFractal(rp);
    float torus = sdTorus(p - vec3(0, sin(t * 0.5) * 0.5, 0), vec2(1.5, 0.3));

    float d = min(fractal, torus);
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

        float marble = sin(p.x * 5.0 + fbm(p * 4.0, 5) * 8.0) * 0.5 + 0.5;
        vec3 baseColor = mix(vec3(0.9, 0.3, 0.1), vec3(0.1, 0.5, 0.9), marble);

        float diff = max(dot(n, lightDir), 0.0);
        float shadow = softShadow(p + n * 0.01, lightDir, 0.02, 5.0);
        float ao = calcAO(p, n);

        vec3 halfway = normalize(lightDir + viewDir);
        float spec = pow(max(dot(n, halfway), 0.0), 128.0);

        float fresnel = pow(1.0 - max(dot(viewDir, n), 0.0), 5.0);

        color = baseColor * (vec3(0.05) * ao + diff * shadow * vec3(1.0, 0.9, 0.8));
        color += spec * shadow * vec3(1.0);
        color += fresnel * vec3(0.2, 0.3, 0.5) * ao;

        float fog = 1.0 - exp(-t * 0.1);
        color = mix(color, vec3(0.05, 0.05, 0.1), fog);
    } else {
        float skyGrad = rd.y * 0.5 + 0.5;
        color = mix(vec3(0.05, 0.05, 0.1), vec3(0.1, 0.15, 0.3), skyGrad);

        float starNoise = fbm(rd * 100.0, 4);
        if (starNoise > 0.85) {
            color += vec3(pow(starNoise - 0.85, 2.0) * 50.0);
        }
    }

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
