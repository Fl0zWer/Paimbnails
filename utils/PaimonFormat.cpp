#include "PaimonFormat.hpp"
#include <fstream>
#include <Geode/loader/Log.hpp>

using namespace geode::prelude;

namespace PaimonFormat {
    bool save(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
        try {
            // Create parent directory
            if (!std::filesystem::exists(path.parent_path())) {
                std::filesystem::create_directories(path.parent_path());
            }
            
            // Encrypt
            auto encrypted = encrypt(data);
            
            // Write file
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) {
                log::error("[PaimonFormat] Failed to open file for writing: {}", path.string());
                return false;
            }
            
            // Magic header: "PAIMON" + version (1 byte)
            const char magic[] = "PAIMON";
            file.write(magic, 6);
            uint8_t version = 2; // v2 includes integrity hash
            file.write(reinterpret_cast<const char*>(&version), 1);
            
            // Data size (4 bytes, little-endian)
            uint32_t size = static_cast<uint32_t>(encrypted.size());
            file.write(reinterpret_cast<const char*>(&size), 4);
            
            // Encrypted data
            file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
            
            // Integrity hash (8 bytes) (v2+)
            uint64_t hash = calculateHash(encrypted);
            file.write(reinterpret_cast<const char*>(&hash), 8);
            
            file.close();
            return true;
        } catch (const std::exception& e) {
            log::error("[PaimonFormat] Failed to save file: {}", e.what());
            return false;
        }
    }
    
    std::vector<uint8_t> load(const std::filesystem::path& path) {
        try {
            if (!std::filesystem::exists(path)) {
                return {};
            }
            
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                log::error("[PaimonFormat] Failed to open file for reading: {}", path.string());
                return {};
            }
            
            // Read and validate magic header
            char magic[6];
            file.read(magic, 6);
            if (std::string(magic, 6) != "PAIMON") {
                log::error("[PaimonFormat] Invalid file format (bad magic header)");
                return {};
            }
            
            // Read version
            uint8_t version;
            file.read(reinterpret_cast<char*>(&version), 1);
            if (version > 2) {
                log::warn("[PaimonFormat] Unsupported future file version: {}", version);
                return {};
            }
            
            // Read size
            uint32_t size;
            file.read(reinterpret_cast<char*>(&size), 4);
            
            if (size == 0 || size > 10 * 1024 * 1024) { // Max 10MB
                log::error("[PaimonFormat] Invalid data size: {}", size);
                return {};
            }
            
            // Read encrypted data
            std::vector<uint8_t> encrypted(size);
            file.read(reinterpret_cast<char*>(encrypted.data()), size);
            
            // Integrity check (v2+)
            if (version >= 2) {
                uint64_t storedHash;
                file.read(reinterpret_cast<char*>(&storedHash), 8);
                
                if (file.gcount() == 8) {
                    uint64_t calculatedHash = calculateHash(encrypted);
                    if (storedHash != calculatedHash) {
                        log::error("[PaimonFormat] Integrity check failed: file was modified or corrupted.");
                        // Corrupt: return empty to force server fetch
                        return {};
                    }
                } else {
                    log::warn("[PaimonFormat] Incomplete v2 file (missing hash)");
                    return {};
                }
            }
            
            file.close();
            
            // Decrypt
            return decrypt(encrypted);
        } catch (const std::exception& e) {
            log::error("[PaimonFormat] Failed to load file: {}", e.what());
            return {};
        }
    }
}

