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
