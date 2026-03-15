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
