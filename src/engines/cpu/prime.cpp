#include "prime.h"

#include <chrono>
#include <cstdint>

#ifdef _MSC_VER
    #define DO_NOT_OPTIMIZE(x) { volatile auto _v = (x); (void)_v; }
#else
    #define DO_NOT_OPTIMIZE(x) asm volatile("" : : "r,m"(x) : "memory")
#endif

namespace occt { namespace cpu {

// Modular exponentiation: (base^exp) mod mod
static uint64_t mod_pow(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t result = 1;
    base %= mod;

    while (exp > 0) {
        if (exp & 1) {
            // Use __uint128_t for overflow-safe multiplication on GCC/Clang
#if defined(__GNUC__) || defined(__clang__)
            result = static_cast<uint64_t>(
                (static_cast<__uint128_t>(result) * base) % mod);
#else
            // MSVC fallback: use _umul128
            uint64_t high;
            uint64_t low = _umul128(result, base, &high);
            uint64_t rem;
            _udiv128(high, low, mod, &rem);
            result = rem;
#endif
        }
#if defined(__GNUC__) || defined(__clang__)
        base = static_cast<uint64_t>(
            (static_cast<__uint128_t>(base) * base) % mod);
#else
        uint64_t high;
        uint64_t low = _umul128(base, base, &high);
        uint64_t rem;
        _udiv128(high, low, mod, &rem);
        base = rem;
#endif
        exp >>= 1;
    }
    return result;
}

bool miller_rabin(uint64_t n, int rounds) {
    if (n < 2) return false;
    if (n == 2 || n == 3) return true;
    if (n % 2 == 0) return false;

    // Write n-1 as 2^r * d
    uint64_t d = n - 1;
    int r = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        ++r;
    }

    // Deterministic witnesses for n < 2^64
    // Using fixed witnesses that cover all 64-bit integers
    static const uint64_t witnesses[] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37
    };

    int num_witnesses = (rounds < 12) ? rounds : 12;

    for (int i = 0; i < num_witnesses; ++i) {
        uint64_t a = witnesses[i];
        if (a >= n) continue;

        uint64_t x = mod_pow(a, d, n);
        if (x == 1 || x == n - 1) continue;

        bool composite = true;
        for (int j = 0; j < r - 1; ++j) {
#if defined(__GNUC__) || defined(__clang__)
            x = static_cast<uint64_t>(
                (static_cast<__uint128_t>(x) * x) % n);
#else
            uint64_t high;
            uint64_t low = _umul128(x, x, &high);
            uint64_t rem;
            _udiv128(high, low, n, &rem);
            x = rem;
#endif
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

bool lucas_lehmer(int p) {
    if (p == 2) return true;
    if (p < 2) return false;

    // For Lucas-Lehmer, we need arbitrary precision for large p.
    // For stress testing, we use a simplified version with 128-bit
    // arithmetic, which works for p up to about 60.

    // M_p = 2^p - 1
    // We work modulo M_p using __uint128_t
#if defined(__GNUC__) || defined(__clang__)
    if (p > 60) {
        // For very large p, fall back to basic computation
        // that still stresses the CPU without needing big integers
        __uint128_t mp = (static_cast<__uint128_t>(1) << p) - 1;
        __uint128_t s = 4;

        for (int i = 0; i < p - 2; ++i) {
            s = (s * s - 2) % mp;
        }
        return s == 0;
    }

    uint64_t mp = (1ULL << p) - 1;
    uint64_t s = 4;

    for (int i = 0; i < p - 2; ++i) {
        // s = (s*s - 2) mod mp
        __uint128_t ss = static_cast<__uint128_t>(s) * s;
        ss -= 2;
        s = static_cast<uint64_t>(ss % mp);
    }
    return s == 0;
#else
    // MSVC: _umul128/_udiv128로 128비트 산술 가능 (p <= 60)
    if (p > 60) return false;
    uint64_t mp = (1ULL << p) - 1;
    uint64_t s = 4;
    for (int i = 0; i < p - 2; ++i) {
        uint64_t high;
        uint64_t low = _umul128(s, s, &high);
        uint64_t rem;
        if (low < 2) {
            // Handle underflow
            low -= 2;
            if (high > 0) --high;
        } else {
            low -= 2;
        }
        _udiv128(high, low, mp, &rem);
        s = rem;
    }
    return s == 0;
#endif
}

uint64_t stress_prime(uint64_t duration_ns) {
    uint64_t ops = 0;
    uint64_t iterations = 0;
    uint64_t candidate = 1000000007ULL; // Start from a known prime region

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // Test primality with Miller-Rabin (20 rounds = heavy computation)
        bool is_prime = miller_rabin(candidate, 20);
        DO_NOT_OPTIMIZE(is_prime);

        // Each Miller-Rabin round involves modular exponentiation
        // Approximate ops: 20 rounds * ~64 squarings * 2 muls = ~2560 ops
        ops += 2560;
        candidate += 2; // Only test odd numbers
        ++iterations;

        // Periodically run Lucas-Lehmer for additional ALU stress
        if ((iterations & 0xFF) == 0) {
            // Test Mersenne primes for various exponents
            for (int p = 3; p <= 31; p += 2) {
                bool is_mersenne = lucas_lehmer(p);
                DO_NOT_OPTIMIZE(is_mersenne);
                ops += static_cast<uint64_t>(p) * p; // Approximate work
            }
        }

        // Check elapsed time every 32 iterations using iteration counter
        // (not ops, which may drift due to variable Lucas-Lehmer additions)
        if ((iterations & 0x1F) == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
            if (elapsed >= duration_ns) break;
        }
    }

    return ops;
}

// ============================================================
// Verification databases - known correct results for error detection
// ============================================================

// Known primes: small primes through large primes (32 entries)
static const uint64_t known_primes[] = {
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
    31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
    104729,                     // 10000th prime
    1299709,                    // 100000th prime
    15485863,                   // 1000000th prime
    179424673,                  // 10000000th prime
    1000000007ULL,              // Well-known large prime
    1000000009ULL,              // Another large prime
    2147483647ULL,              // 2^31 - 1 (Mersenne prime M31)
    4294967291ULL,              // Largest 32-bit prime
    4294967311ULL,              // Prime just above 2^32
    1099511627791ULL,           // Large 40-bit prime
    6700417,                    // Factor of F5 (Fermat), itself prime
    7919                        // 1000th prime
};
static const int known_primes_count = sizeof(known_primes) / sizeof(known_primes[0]);

// Known composites: includes Carmichael numbers and other tricky composites (24 entries)
// Carmichael numbers are pseudoprimes to all bases coprime to them
static const uint64_t known_composites[] = {
    4, 6, 8, 9, 10, 15, 25, 49,
    561,                        // Smallest Carmichael number (3 * 11 * 17)
    1105,                       // Carmichael: 5 * 13 * 17
    1729,                       // Carmichael: 7 * 13 * 19 (Hardy-Ramanujan number)
    2465,                       // Carmichael: 5 * 17 * 29
    2821,                       // Carmichael: 7 * 13 * 31
    6601,                       // Carmichael: 7 * 23 * 41
    8911,                       // Carmichael: 7 * 19 * 67
    10585,                      // Carmichael: 5 * 29 * 73
    15841,                      // Carmichael: 7 * 31 * 73
    29341,                      // Carmichael: 13 * 37 * 61
    41041,                      // Carmichael: 7 * 11 * 13 * 41
    46657,                      // Carmichael: 13 * 37 * 97
    52633,                      // Carmichael: 7 * 73 * 103
    62745,                      // Carmichael: 3 * 5 * 47 * 89
    63973,                      // Carmichael: 7 * 13 * 19 * 37
    75361                       // Carmichael: 11 * 13 * 17 * 31
};
static const int known_composites_count = sizeof(known_composites) / sizeof(known_composites[0]);

// Known Mersenne prime exponents: p such that 2^p - 1 is prime
// Only include exponents that our Lucas-Lehmer can handle (p <= 60 with __uint128_t)
static const int known_mersenne_exponents[] = {
    2, 3, 5, 7, 13, 17, 19, 31
};
static const int known_mersenne_count = sizeof(known_mersenne_exponents) / sizeof(known_mersenne_exponents[0]);

// Known non-Mersenne exponents: p where 2^p - 1 is NOT prime (p <= 60, p is prime)
static const int known_non_mersenne_exponents[] = {
    11, 23, 29, 37, 41, 43, 47, 53, 59
};
static const int known_non_mersenne_count = sizeof(known_non_mersenne_exponents) / sizeof(known_non_mersenne_exponents[0]);

PrimeVerifyResult verify_miller_rabin() {
    PrimeVerifyResult result;
    result.passed = true;
    result.tests_run = 0;
    result.errors_found = 0;

    // Test known primes - should all return true
    for (int i = 0; i < known_primes_count; ++i) {
        bool got = miller_rabin(known_primes[i], 20);
        ++result.tests_run;
        if (!got) {
            ++result.errors_found;
            if (result.passed) {
                result.passed = false;
                result.first_error_number = known_primes[i];
                result.expected_prime = true;
                result.got_prime = false;
            }
        }
    }

    // Test known composites - should all return false
    for (int i = 0; i < known_composites_count; ++i) {
        bool got = miller_rabin(known_composites[i], 20);
        ++result.tests_run;
        if (got) {
            ++result.errors_found;
            if (result.passed) {
                result.passed = false;
                result.first_error_number = known_composites[i];
                result.expected_prime = false;
                result.got_prime = true;
            }
        }
    }

    return result;
}

PrimeVerifyResult verify_lucas_lehmer() {
    PrimeVerifyResult result;
    result.passed = true;
    result.tests_run = 0;
    result.errors_found = 0;

    // Test known Mersenne prime exponents - should all return true
    for (int i = 0; i < known_mersenne_count; ++i) {
        int p = known_mersenne_exponents[i];
        bool got = lucas_lehmer(p);
        ++result.tests_run;
        if (!got) {
            ++result.errors_found;
            if (result.passed) {
                result.passed = false;
                result.first_error_number = static_cast<uint64_t>(p);
                result.expected_prime = true;
                result.got_prime = false;
            }
        }
    }

    // Test known non-Mersenne exponents - should all return false
    for (int i = 0; i < known_non_mersenne_count; ++i) {
        int p = known_non_mersenne_exponents[i];
        bool got = lucas_lehmer(p);
        ++result.tests_run;
        if (got) {
            ++result.errors_found;
            if (result.passed) {
                result.passed = false;
                result.first_error_number = static_cast<uint64_t>(p);
                result.expected_prime = false;
                result.got_prime = true;
            }
        }
    }

    return result;
}

PrimeStressResult stress_prime_verified(uint64_t duration_ns) {
    PrimeStressResult result;
    result.ops = 0;
    result.verified = true;

    uint64_t iterations = 0;
    uint64_t candidate = 1000000007ULL; // Start from a known prime region

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // Test primality with Miller-Rabin (20 rounds = heavy computation)
        bool is_prime = miller_rabin(candidate, 20);
        DO_NOT_OPTIMIZE(is_prime);

        result.ops += 2560;
        candidate += 2;
        ++iterations;

        // Periodically run Lucas-Lehmer for additional ALU stress
        if ((iterations & 0xFF) == 0) {
            for (int p = 3; p <= 31; p += 2) {
                bool is_mersenne = lucas_lehmer(p);
                DO_NOT_OPTIMIZE(is_mersenne);
                result.ops += static_cast<uint64_t>(p) * p;
            }
        }

        // Run verification every 1024 iterations
        if ((iterations & 0x3FF) == 0) {
            PrimeVerifyResult mr = verify_miller_rabin();
            result.mr_verify = mr;
            // Count verification ops: known_primes_count + known_composites_count MR tests
            result.ops += static_cast<uint64_t>(mr.tests_run) * 2560;

            PrimeVerifyResult ll = verify_lucas_lehmer();
            result.ll_verify = ll;
            // Count verification ops for LL tests
            for (int i = 0; i < known_mersenne_count; ++i) {
                int p = known_mersenne_exponents[i];
                result.ops += static_cast<uint64_t>(p) * p;
            }
            for (int i = 0; i < known_non_mersenne_count; ++i) {
                int p = known_non_mersenne_exponents[i];
                result.ops += static_cast<uint64_t>(p) * p;
            }

            if (!mr.passed || !ll.passed) {
                result.verified = false;
                // Don't break - keep stressing, let caller handle the error
            }
        }

        // Check elapsed time every 32 iterations
        if ((iterations & 0x1F) == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
            if (elapsed >= duration_ns) break;
        }
    }

    // Final verification at the end of the batch
    if (result.verified) {
        PrimeVerifyResult mr = verify_miller_rabin();
        result.mr_verify = mr;
        PrimeVerifyResult ll = verify_lucas_lehmer();
        result.ll_verify = ll;
        if (!mr.passed || !ll.passed) {
            result.verified = false;
        }
    }

    return result;
}

}} // namespace occt::cpu
