// ─── Walking Ones Test ───────────────────────────────────────────────────────
// Writes a single-bit pattern and verifies readback.
__kernel void walking_ones(
    __global uint* buffer,
    __global uint* errors,
    const uint pattern_shift)
{
    int gid = get_global_id(0);
    uint pattern = 1u << (pattern_shift % 32);

    buffer[gid] = pattern;
    barrier(CLK_GLOBAL_MEM_FENCE);

    uint read_val = buffer[gid];
    if (read_val != pattern) {
        atomic_inc(&errors[0]);
    }
}

// ─── Walking Zeros Test ──────────────────────────────────────────────────────
// Complement of walking ones.
__kernel void walking_zeros(
    __global uint* buffer,
    __global uint* errors,
    const uint pattern_shift)
{
    int gid = get_global_id(0);
    uint pattern = ~(1u << (pattern_shift % 32));

    buffer[gid] = pattern;
    barrier(CLK_GLOBAL_MEM_FENCE);

    uint read_val = buffer[gid];
    if (read_val != pattern) {
        atomic_inc(&errors[0]);
    }
}

// ─── Address Test ────────────────────────────────────────────────────────────
// Each work-item writes its own global ID and reads it back.
__kernel void address_test(
    __global uint* buffer,
    __global uint* errors)
{
    int gid = get_global_id(0);
    buffer[gid] = (uint)gid;
    barrier(CLK_GLOBAL_MEM_FENCE);

    uint read_val = buffer[gid];
    if (read_val != (uint)gid) {
        atomic_inc(&errors[0]);
    }
}

// ─── Alternating Pattern Test (0xAA / 0x55) ─────────────────────────────────
// Tests adjacent bit interference.
__kernel void alternating_pattern(
    __global uint* buffer,
    __global uint* errors,
    const uint phase)
{
    int gid = get_global_id(0);
    uint pattern = (phase == 0) ? 0xAAAAAAAAu : 0x55555555u;

    buffer[gid] = pattern;
    barrier(CLK_GLOBAL_MEM_FENCE);

    uint read_val = buffer[gid];
    if (read_val != pattern) {
        atomic_inc(&errors[0]);
    }
}

// ─── Block Pattern Test ─────────────────────────────────────────────────────
// Writes alternating 0x00000000 / 0xFFFFFFFF blocks.
__kernel void block_pattern(
    __global uint* buffer,
    __global uint* errors,
    const uint phase)
{
    int gid = get_global_id(0);
    uint pattern = ((gid + phase) & 1) ? 0xFFFFFFFFu : 0x00000000u;

    buffer[gid] = pattern;
    barrier(CLK_GLOBAL_MEM_FENCE);

    uint read_val = buffer[gid];
    if (read_val != pattern) {
        atomic_inc(&errors[0]);
    }
}
