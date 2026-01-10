#include "GIFDecoder.hpp"
#include <Geode/loader/Log.hpp>
#include <cstring>
#include <algorithm>
#include <utility>

using namespace geode::prelude;

bool GIFDecoder::isGIF(const uint8_t* data, size_t size) {
    if (size < 6) return false;
    // Check GIF87a or GIF89a signature
    return (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0);
}

bool GIFDecoder::getDimensions(const uint8_t* data, size_t size, int& width, int& height) {
    if (!isGIF(data, size)) return false;
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    return parseHeader(ptr, end, width, height);
}

GIFDecoder::GIFData GIFDecoder::decode(const uint8_t* data, size_t size) {
    GIFData result;
    result.isAnimated = false;
    
    if (!isGIF(data, size)) {
        log::error("[GIFDecoder] Not a valid GIF");
        return result;
    }

    const uint8_t* ptr = data;
    const uint8_t* end = data + size;

    // Parse header
    if (!parseHeader(ptr, end, result.width, result.height)) {
        log::error("[GIFDecoder] Failed to parse header");
        return result;
    }

    // Read flags
    if (ptr >= end) return result;
    uint8_t flags = *ptr++;
    bool hasGlobalColorTable = (flags & 0x80) != 0;
    int colorResolution = ((flags & 0x70) >> 4) + 1;
    int globalColorTableSize = 1 << ((flags & 0x07) + 1);

    // Skip background color and aspect ratio
    ptr += 2;

    // Read global color table
    std::vector<uint8_t> globalPalette;
    if (hasGlobalColorTable) {
        if (!parseColorTable(ptr, end, globalPalette, globalColorTableSize)) {
            log::error("[GIFDecoder] Failed to parse global color table");
            return result;
        }
    }

    // Parse frames
    int frameCount = 0;
    int currentDelay = 100; // Default delay
    int transparentIndex = -1;
    bool hasTransparency = false;
    int disposalMethod = 0; // 0: Unspecified, 1: Do not dispose, 2: Restore to BG, 3: Restore to Prev

    // Canvas for composition
    std::vector<uint8_t> canvas(result.width * result.height * 4, 0);
    std::vector<uint8_t> backupCanvas = canvas;
    
    int prevDisposal = 0;
    RawFrame prevRawFrame = {std::vector<uint8_t>(), 0, 0, 0, 0};

    while (ptr < end && frameCount < 500) { // Increased limit
        if (*ptr == 0x21) { // Extension block
            ptr++;
            if (ptr >= end) break;
            uint8_t label = *ptr++;
            
            // Graphic Control Extension (animation delays)
            if (label == 0xF9) {
                if (ptr + 1 >= end) break;
                uint8_t blockSize = *ptr++;
                if (blockSize == 4 && ptr + 4 <= end) {
                    uint8_t packed = *ptr++;
                    uint16_t delay = ptr[0] | (ptr[1] << 8);
                    uint8_t transIdx = ptr[2];
                    ptr += 3;
                    
                    currentDelay = (delay == 0) ? 100 : delay * 10; // GIF uses hundredths of a second
                    hasTransparency = (packed & 1) != 0;
                    transparentIndex = transIdx;
                    disposalMethod = (packed >> 2) & 0x07;
                } else {
                    ptr += blockSize;
                }
                // Skip block terminator
                if (ptr < end && *ptr == 0) ptr++;
            } else {
                // Skip other extensions
                while (ptr < end) {
                    uint8_t blockSize = *ptr++;
                    if (blockSize == 0) break;
                    ptr += blockSize;
                }
            }
        } else if (*ptr == 0x2C) { // Image descriptor
            RawFrame rawFrame;
            if (parseFrame(ptr, end, rawFrame, globalPalette, transparentIndex, hasTransparency)) {
                
                // 1. Handle disposal of PREVIOUS frame
                if (prevDisposal == 2) {
                    // Restore to background (transparent)
                    for (int y = 0; y < prevRawFrame.height; y++) {
                        for (int x = 0; x < prevRawFrame.width; x++) {
                            int cy = prevRawFrame.top + y;
                            int cx = prevRawFrame.left + x;
                            if (cx >= 0 && cx < result.width && cy >= 0 && cy < result.height) {
                                int idx = (cy * result.width + cx) * 4;
                                canvas[idx] = 0;
                                canvas[idx+1] = 0;
                                canvas[idx+2] = 0;
                                canvas[idx+3] = 0;
                            }
                        }
                    }
                } else if (prevDisposal == 3) {
                    // Restore to previous
                    canvas = backupCanvas;
                }
                
                // 2. Save state for NEXT frame if CURRENT disposal is 3
                if (disposalMethod == 3) {
                    backupCanvas = canvas;
                }
                
                // 3. Draw current frame onto canvas
                for (int y = 0; y < rawFrame.height; y++) {
                    for (int x = 0; x < rawFrame.width; x++) {
                        int cy = rawFrame.top + y;
                        int cx = rawFrame.left + x;
                        if (cx >= 0 && cx < result.width && cy >= 0 && cy < result.height) {
                            int rawIdx = (y * rawFrame.width + x) * 4;
                            int canvasIdx = (cy * result.width + cx) * 4;
                            
                            if (rawFrame.pixels[rawIdx + 3] > 0) {
                                canvas[canvasIdx] = rawFrame.pixels[rawIdx];
                                canvas[canvasIdx+1] = rawFrame.pixels[rawIdx+1];
                                canvas[canvasIdx+2] = rawFrame.pixels[rawIdx+2];
                                canvas[canvasIdx+3] = rawFrame.pixels[rawIdx+3];
                            }
                        }
                    }
                }
                
                // 4. Push result (Full canvas)
                Frame frame;
                frame.left = 0;
                frame.top = 0;
                frame.width = result.width;
                frame.height = result.height;
                frame.delayMs = currentDelay;
                frame.pixels = canvas;
                
                result.frames.push_back(frame);
                frameCount++;
                
                // 5. Update state
                prevDisposal = disposalMethod;
                prevRawFrame = rawFrame;
                
            } else {
                break;
            }
        } else if (*ptr == 0x3B) { // Trailer (end of GIF)
            break;
        } else {
            ptr++; // Skip unknown byte
        }
    }

    result.isAnimated = result.frames.size() > 1;
    log::info("[GIFDecoder] Decodificados {} frames ({}x{})", result.frames.size(), result.width, result.height);
    
    return result;
}

bool GIFDecoder::parseHeader(const uint8_t*& ptr, const uint8_t* end, int& width, int& height) {
    if (ptr + 13 > end) return false;
    
    ptr += 6; // Skip signature
    width = ptr[0] | (ptr[1] << 8);
    height = ptr[2] | (ptr[3] << 8);
    ptr += 4;
    
    return width > 0 && height > 0 && width <= 4096 && height <= 4096;
}

bool GIFDecoder::parseColorTable(const uint8_t*& ptr, const uint8_t* end, std::vector<uint8_t>& palette, int size) {
    if (ptr + size * 3 > end) return false;
    
    palette.resize(size * 3);
    memcpy(palette.data(), ptr, size * 3);
    ptr += size * 3;
    
    return true;
}

// LZW Decompression
static bool lzwDecode(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& output, int minCodeSize, int pixelCount) {
    int clearCode = 1 << minCodeSize;
    int eoiCode = clearCode + 1;
    int nextCode = eoiCode + 1;
    int currentCodeSize = minCodeSize + 1;
    int codeMask = (1 << currentCodeSize) - 1;

    // Dictionary
    struct DictEntry {
        int prefix = -1;
        uint8_t suffix = 0;
        int length = 0;
    };
    // Pre-allocate dictionary (max 4096 entries)
    std::vector<DictEntry> dictionary(4096);
    
    // Initialize base dictionary
    for (int i = 0; i < clearCode; ++i) {
        dictionary[i] = { -1, (uint8_t)i, 1 };
    }
    
    int dictSize = eoiCode + 1;
    int oldCode = -1;
    
    // Bit reading state
    int bitPos = 0;
    int bytePos = 0;
    
    output.reserve(pixelCount);
    
    while (output.size() < pixelCount) {
        // Read code
        int code = 0;
        for (int i = 0; i < currentCodeSize; ++i) {
            if (bytePos >= compressed.size()) break;
            if ((compressed[bytePos] >> bitPos) & 1) {
                code |= (1 << i);
            }
            bitPos++;
            if (bitPos == 8) {
                bitPos = 0;
                bytePos++;
            }
        }
        
        if (code == clearCode) {
            currentCodeSize = minCodeSize + 1;
            codeMask = (1 << currentCodeSize) - 1;
            dictSize = eoiCode + 1;
            nextCode = eoiCode + 1;
            oldCode = -1;
            continue;
        }
        
        if (code == eoiCode) break;
        
        if (oldCode == -1) {
            if (code < dictSize) {
                output.push_back(dictionary[code].suffix);
                oldCode = code;
            }
            continue;
        }
        
        int inCode = code;
        std::vector<uint8_t> sequence;
        
        if (code >= dictSize) {
            if (code == dictSize) {
                // Special case: code = nextCode
                // Output = oldCode + first char of oldCode
                int temp = oldCode;
                while (temp != -1) {
                    sequence.push_back(dictionary[temp].suffix);
                    temp = dictionary[temp].prefix;
                }
                std::reverse(sequence.begin(), sequence.end());
                sequence.push_back(sequence[0]);
            } else {
                // Invalid code
                return false;
            }
        } else {
            int temp = code;
            while (temp != -1) {
                sequence.push_back(dictionary[temp].suffix);
                temp = dictionary[temp].prefix;
            }
            std::reverse(sequence.begin(), sequence.end());
        }
        
        output.insert(output.end(), sequence.begin(), sequence.end());
        
        // Add to dictionary
        if (dictSize < 4096) {
            int temp = oldCode;
            // Find first char of sequence (which is just sequence[0])
            // But we need to reconstruct it from oldCode + first char of new sequence
            // Actually, the new entry is oldCode + first char of the sequence we just output
            
            uint8_t firstChar = sequence[0];
            dictionary[dictSize] = { oldCode, firstChar, dictionary[oldCode].length + 1 };
            dictSize++;
            
            if (dictSize >= (1 << currentCodeSize) && currentCodeSize < 12) {
                currentCodeSize++;
            }
        }
        
        oldCode = inCode;
    }
    
    return true;
}

bool GIFDecoder::parseFrame(const uint8_t*& ptr, const uint8_t* end, RawFrame& frame, const std::vector<uint8_t>& globalPalette, int transparentIndex, bool hasTransparency) {
    if (ptr + 10 > end) return false;
    
    ptr++; // Skip image separator
    
    // Read frame dimensions
    frame.left = ptr[0] | (ptr[1] << 8);
    frame.top = ptr[2] | (ptr[3] << 8);
    frame.width = ptr[4] | (ptr[5] << 8);
    frame.height = ptr[6] | (ptr[7] << 8);
    uint8_t flags = ptr[8];
    ptr += 9;
    
    bool hasLocalColorTable = (flags & 0x80) != 0;
    int localColorTableSize = hasLocalColorTable ? (1 << ((flags & 0x07) + 1)) : 0;
    bool interlaced = (flags & 0x40) != 0;
    
    // Read local color table if present
    std::vector<uint8_t> localPalette;
    if (hasLocalColorTable) {
        if (!parseColorTable(ptr, end, localPalette, localColorTableSize)) {
            return false;
        }
    }
    
    const std::vector<uint8_t>& palette = hasLocalColorTable ? localPalette : globalPalette;
    
    // Skip LZW minimum code size
    if (ptr >= end) return false;
    uint8_t lzwMinCodeSize = *ptr++;
    
    // Read compressed data
    std::vector<uint8_t> compressedData;
    while (ptr < end) {
        uint8_t blockSize = *ptr++;
        if (blockSize == 0) break;
        if (ptr + blockSize > end) return false;
        compressedData.insert(compressedData.end(), ptr, ptr + blockSize);
        ptr += blockSize;
    }
    
    // LZW decompress
    std::vector<uint8_t> indices;
    if (!lzwDecode(compressedData, indices, lzwMinCodeSize, frame.width * frame.height)) {
        log::error("[GIFDecoder] Error descomprimiendo LZW");
        return false;
    }
    
    // Build RGBA frame
    frame.pixels.resize(frame.width * frame.height * 4);
    
    // Handle interlacing
    std::vector<uint8_t> deinterlacedIndices(frame.width * frame.height);
    if (interlaced) {
        int passOffsets[] = {0, 4, 2, 1};
        int passInc[] = {8, 8, 4, 2};
        
        int currentPass = 0;
        int currentY = 0;
        int currentX = 0;
        
        for (uint8_t idx : indices) {
            if (currentY >= frame.height) break;
            
            deinterlacedIndices[currentY * frame.width + currentX] = idx;
            
            currentX++;
            if (currentX == frame.width) {
                currentX = 0;
                currentY += passInc[currentPass];
                if (currentY >= frame.height) {
                    currentPass++;
                    if (currentPass < 4) {
                        currentY = passOffsets[currentPass];
                    }
                }
            }
        }
    } else {
        deinterlacedIndices = indices;
    }
    
    // Convert indices to RGBA
    for (int i = 0; i < frame.width * frame.height; i++) {
        if (i >= deinterlacedIndices.size()) break;
        
        uint8_t colorIndex = deinterlacedIndices[i];
        
        if (hasTransparency && colorIndex == transparentIndex) {
            frame.pixels[i * 4 + 0] = 0;
            frame.pixels[i * 4 + 1] = 0;
            frame.pixels[i * 4 + 2] = 0;
            frame.pixels[i * 4 + 3] = 0;
        } else {
            if (colorIndex * 3 + 2 < palette.size()) {
                frame.pixels[i * 4 + 0] = palette[colorIndex * 3 + 0];
                frame.pixels[i * 4 + 1] = palette[colorIndex * 3 + 1];
                frame.pixels[i * 4 + 2] = palette[colorIndex * 3 + 2];
                frame.pixels[i * 4 + 3] = 255;
            } else {
                // Index out of bounds
                frame.pixels[i * 4 + 0] = 0;
                frame.pixels[i * 4 + 1] = 0;
                frame.pixels[i * 4 + 2] = 0;
                frame.pixels[i * 4 + 3] = 255;
            }
        }
    }
    
    return true;
}

