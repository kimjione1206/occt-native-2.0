#pragma once

#include <cstdint>

namespace occt { namespace cpu {

// Run prime-finding stress for specified duration (nanoseconds)
// Returns total operations executed
uint64_t stress_prime(uint64_t duration_ns);

// Miller-Rabin primality test
bool miller_rabin(uint64_t n, int rounds = 20);

// Lucas-Lehmer primality test for Mersenne numbers (2^p - 1)
bool lucas_lehmer(int p);

// Verification result for prime correctness checks
struct PrimeVerifyResult {
    bool passed = true;         // All tests matched expected results
    int tests_run = 0;          // Number of known values tested
    int errors_found = 0;       // Number of mismatches
    uint64_t first_error_number = 0; // First number that produced wrong result
    bool expected_prime = false;     // Expected primality for first error
    bool got_prime = false;          // Actual primality for first error
};

// Verify Miller-Rabin against known primes/composites database
PrimeVerifyResult verify_miller_rabin();

// Verify Lucas-Lehmer against known Mersenne prime exponents
PrimeVerifyResult verify_lucas_lehmer();

// Stress result with verification information
struct PrimeStressResult {
    uint64_t ops = 0;           // Total operations executed
    bool verified = true;       // Verification passed (or not performed)
    PrimeVerifyResult mr_verify;  // Miller-Rabin verification details
    PrimeVerifyResult ll_verify;  // Lucas-Lehmer verification details
};

// Run prime stress with periodic verification
PrimeStressResult stress_prime_verified(uint64_t duration_ns);

}} // namespace occt::cpu
