#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace PaimonFormat {
    // XOR key (replace for production)
    constexpr uint8_t XOR_KEY[] = {0x50, 0x41, 0x49, 0x4D, 0x4F, 0x4E, 0x5F, 0x53, 0x45, 0x43, 0x52, 0x45, 0x54}; // "PAIMON_SECRET"
    constexpr size_t KEY_LENGTH = 13;
    
    // Hash salt
    constexpr uint64_t HASH_SALT = 0x9E3779B97F4A7C15;

    // Calcular FNV-1a 64-bit hash
    inline uint64_t calculateHash(const std::vector<uint8_t>& data) {
        uint64_t hash = 0xCBF29CE484222325; // FNV offset basis
        
        // Hash salt first
        for (int i = 0; i < 8; i++) {
            hash ^= ((HASH_SALT >> (i * 8)) & 0xFF);
            hash *= 0x100000001B3; // FNV prime
        }

        // Hash data
        for (uint8_t byte : data) {
            hash ^= byte;
            hash *= 0x100000001B3;
        }
        return hash;
    }

    // Encrypt using XOR (rotating key)
    inline std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> encrypted(data.size());
        for (size_t i = 0; i < data.size(); i++) {
            encrypted[i] = data[i] ^ XOR_KEY[i % KEY_LENGTH];
        }
        return encrypted;
    }
    
    // Decrypt (XOR is symmetric)
    inline std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) {
        return encrypt(data); // XOR is its own inverse
    }
    
    // Save encrypted data to a .paimon file
    bool save(const std::filesystem::path& path, const std::vector<uint8_t>& data);
    
    // Load and decrypt data from a .paimon file
    std::vector<uint8_t> load(const std::filesystem::path& path);
}

