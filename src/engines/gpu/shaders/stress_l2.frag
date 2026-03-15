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

    vec3 ambient = vec3(0.08, 0.08, 0.12);

    float diff1 = max(dot(normal, lightDir), 0.0);
    float diff2 = max(dot(normal, lightDir2), 0.0);
    vec3 diffuse = diff1 * vec3(0.7, 0.3, 0.2) + diff2 * vec3(0.2, 0.3, 0.7) * 0.5;

    vec3 halfway = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfway), 0.0), 64.0);
    vec3 specular = spec * vec3(1.0, 0.9, 0.8) * 0.8;

    float fresnel = pow(1.0 - max(dot(viewDir, normal), 0.0), 3.0);
    vec3 rim = fresnel * vec3(0.3, 0.4, 0.6);

    outColor = vec4(ambient + diffuse + specular + rim, 1.0);
}
