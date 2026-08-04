#include <shiki/render/shader_compiler.h>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <vector>

namespace shiki {

ShaderData ShaderCompiler::loadFromFile(const std::string& path, ShaderType type) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        spdlog::error("Failed to open shader file: {}", path);
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        spdlog::error("Failed to read shader file: {}", path);
        return {};
    }

    ShaderFormat format = ShaderFormat::SPIRV;
    const bool has5 = path.size() >= 5;
    if (has5 && (path.compare(path.size() - 5, 5, ".dxil") == 0 ||
                 path.compare(path.size() - 5, 5, ".cso") == 0)) {
        format = ShaderFormat::DXIL;
    } else if (has5 && path.compare(path.size() - 5, 5, ".dxbc") == 0) {
        format = ShaderFormat::DXBC;
    } else if (path.size() >= 8 &&
               path.compare(path.size() - 8, 8, ".metallib") == 0) {
        format = ShaderFormat::METALLIB;
    }

    return loadFromMemory(buffer.data(), buffer.size(), format, type);
}

ShaderData ShaderCompiler::loadFromMemory(const uint8_t* data, size_t size, ShaderFormat format, ShaderType type) {
    ShaderData shader;
    shader.bytecode.assign(data, data + size);
    shader.format = format;
    shader.type = type;
    shader.entryPoint = format == ShaderFormat::METALLIB
                            ? (type == ShaderType::Vertex ? "spriteVertex"
                                                          : "spriteFragment")
                            : "main";
    return shader;
}

ShaderFormat ShaderCompiler::getSupportedFormat() {
    #if defined(SHIKI_SHADER_FORMAT_DXIL)
        return ShaderFormat::DXIL;
    #elif defined(SHIKI_SHADER_FORMAT_SPIRV)
        return ShaderFormat::SPIRV;
    #else
        #if defined(_WIN32)
            return ShaderFormat::DXIL;
        #elif defined(__APPLE__)
            return ShaderFormat::METALLIB;
        #else
            return ShaderFormat::SPIRV;
        #endif
    #endif
}

std::string ShaderCompiler::formatToString(ShaderFormat format) {
    switch (format) {
        case ShaderFormat::SPIRV: return "SPIR-V";
        case ShaderFormat::DXIL: return "DXIL";
        case ShaderFormat::DXBC: return "DXBC";
        case ShaderFormat::METALLIB: return "Metal library";
        default: return "Unknown";
    }
}

std::string ShaderCompiler::typeToString(ShaderType type) {
    switch (type) {
        case ShaderType::Vertex: return "Vertex";
        case ShaderType::Fragment: return "Fragment";
        case ShaderType::Compute: return "Compute";
        default: return "Unknown";
    }
}

std::string ShaderCompiler::getShaderExtension(ShaderFormat format) {
    switch (format) {
        case ShaderFormat::SPIRV: return ".spv";
        case ShaderFormat::DXIL: return ".dxil";
        case ShaderFormat::DXBC: return ".dxbc";
        case ShaderFormat::METALLIB: return ".metallib";
        default: return ".bin";
    }
}

} // namespace shiki
