#pragma once

#include <string>
#include <vector>

namespace occt {

class CertStore {
public:
    /// @param db_path  Path to the certificate DB file. Empty string uses default config dir.
    explicit CertStore(const std::string& db_path = "");

    /// Store a certificate JSON string (keyed by SHA-256 hash).
    bool submit(const std::string& cert_json);

    /// Retrieve a certificate JSON by its hash.
    std::string lookup(const std::string& hash) const;

    /// Verify a hash exists and the stored data is intact.
    bool verify(const std::string& hash) const;

    /// List all stored certificate hashes.
    std::vector<std::string> list_hashes() const;

private:
    std::string db_path_;

    // Simple JSON file storage: { "certs": { "hash1": {...}, "hash2": {...} } }
};

} // namespace occt
