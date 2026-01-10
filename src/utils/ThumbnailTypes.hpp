#pragma once
#include <string>

struct ThumbnailInfo {
    std::string id;
    std::string url;
    std::string type; // "static" or "gif"
    std::string format;
};
