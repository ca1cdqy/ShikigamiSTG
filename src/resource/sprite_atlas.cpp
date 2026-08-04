#include <shiki/resource/sprite_atlas.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>

#include <cctype>
#include <algorithm>

namespace shiki {

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string parseString(const std::string& json, size_t& pos) {
    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";
    pos++;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    std::string result = json.substr(pos, end - pos);
    pos = end + 1;
    return result;
}

static int parseInt(const std::string& json, size_t& pos) {
    while (pos < json.size() && !std::isdigit(json[pos]) && json[pos] != '-') {
        pos++;
    }
    if (pos >= json.size()) return 0;
    size_t start = pos;
    if (json[pos] == '-') pos++;
    while (pos < json.size() && std::isdigit(json[pos])) pos++;
    return std::stoi(json.substr(start, pos - start));
}

static float parseFloat(const std::string& json, size_t& pos) {
    while (pos < json.size() && !std::isdigit(json[pos]) && json[pos] != '-' && json[pos] != '.') {
        pos++;
    }
    if (pos >= json.size()) return 0.0f;
    size_t start = pos;
    if (json[pos] == '-') pos++;
    while (pos < json.size() && (std::isdigit(json[pos]) || json[pos] == '.')) pos++;
    return std::stof(json.substr(start, pos - start));
}

bool SpriteAtlasLoader::loadFromJson(const std::string& jsonPath, SpriteAtlas& atlas) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        spdlog::error("Failed to open JSON file: {}", jsonPath);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonContent = buffer.str();

    return parseJson(jsonContent, atlas);
}

bool SpriteAtlasLoader::parseJson(const std::string& jsonContent, SpriteAtlas& atlas) {
    size_t pos = 0;

    pos = jsonContent.find("\"source\"", pos);
    if (pos != std::string::npos) {
        pos = jsonContent.find(':', pos);
        atlas.source = parseString(jsonContent, pos);
    }

    pos = jsonContent.find("\"textures\"", 0);
    if (pos != std::string::npos) {
        pos = jsonContent.find('[', pos);
        if (pos != std::string::npos) {
            pos++;
            while (pos < jsonContent.size()) {
                while (pos < jsonContent.size() && std::isspace(jsonContent[pos])) pos++;
                if (jsonContent[pos] == ']') break;
                if (jsonContent[pos] == '"') {
                    std::string texture = parseString(jsonContent, pos);
                    if (!texture.empty()) {
                        atlas.textures.push_back(texture);
                    }
                }
                pos++;
            }
        }
    }

    pos = jsonContent.find("\"sprites\"", 0);
    if (pos != std::string::npos) {
        pos = jsonContent.find('[', pos);
        if (pos != std::string::npos) {
            while (true) {
                pos = jsonContent.find('{', pos);
                if (pos == std::string::npos) break;
                pos++;

                SpriteFrame frame;
                bool foundEnd = false;

                while (pos < jsonContent.size() && !foundEnd) {
                    while (pos < jsonContent.size() && std::isspace(jsonContent[pos])) pos++;

                    if (jsonContent[pos] == '}') {
                        foundEnd = true;
                        pos++;
                        break;
                    }

                    if (jsonContent[pos] == '"') {
                        std::string key = parseString(jsonContent, pos);

                        while (pos < jsonContent.size() && (std::isspace(jsonContent[pos]) || jsonContent[pos] == ':')) pos++;

                        if (key == "id") {
                            frame.id = parseInt(jsonContent, pos);
                        } else if (key == "file") {
                            frame.file = parseString(jsonContent, pos);
                        } else if (key == "width") {
                            frame.width = parseInt(jsonContent, pos);
                        } else if (key == "height") {
                            frame.height = parseInt(jsonContent, pos);
                        } else if (key == "origin_x") {
                            frame.originX = parseFloat(jsonContent, pos);
                        } else if (key == "origin_y") {
                            frame.originY = parseFloat(jsonContent, pos);
                        }
                    }
                    pos++;
                }

                if (!frame.file.empty()) {
                    atlas.sprites.push_back(frame);
                }

                size_t nextObj = jsonContent.find('{', pos);
                size_t nextArray = jsonContent.find(']', pos);
                if (nextArray != std::string::npos && (nextObj == std::string::npos || nextArray < nextObj)) {
                    break;
                }
            }
        }
    }

    spdlog::info("Loaded sprite atlas: {} with {} sprites", atlas.source, atlas.sprites.size());
    return true;
}

bool SpriteAtlasLoader::loadFromDirectory(const std::string& dirPath, std::vector<SpriteAtlas>& atlases) {
    spdlog::info("Loading sprite atlases from: {}", dirPath);

    return true;
}

} // namespace shiki
