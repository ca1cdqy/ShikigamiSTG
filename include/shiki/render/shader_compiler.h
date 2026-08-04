#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace shiki {

/** Identifies the binary representation of a compiled shader. */
enum class ShaderFormat {
	SPIRV, ///< SPIR-V bytecode used by Vulkan.
	DXIL,  ///< DXIL bytecode used by Direct3D 12.
	DXBC,  ///< DXBC bytecode used by Direct3D 11.
	METALLIB ///< Apple Metal library bytecode.
};

/** Identifies the programmable GPU pipeline stage a shader targets. */
enum class ShaderType {
	Vertex,   ///< Vertex shader stage.
	Fragment, ///< Fragment (pixel) shader stage.
	Compute   ///< Compute shader stage.
};

/** Holds the bytecode and metadata for one precompiled shader. */
struct ShaderData {
	std::vector<uint8_t> bytecode; ///< Compiled shader bytecode.
	ShaderFormat format;           ///< Binary format of the bytecode.
	ShaderType type;               ///< Pipeline stage targeted by the shader.
	std::string entryPoint;        ///< Entry-point function name in the shader source.

	/** Returns whether the bytecode buffer is non-empty. */
	bool isValid() const { return !bytecode.empty(); }
};

/**
 * Static helper that loads and identifies precompiled GPU shaders.
 *
 * All methods are stateless and can be called at any point after the GPU
 * device is available. getSupportedFormat() determines which format to
 * request on the current platform.
 */
class ShaderCompiler {
  public:
	/** Reads a precompiled shader file and returns its bytecode and metadata. */
	static ShaderData loadFromFile(const std::string &path, ShaderType type);

	/** Wraps a caller-owned precompiled bytecode buffer without copying. */
	static ShaderData loadFromMemory(const uint8_t *data, size_t size,
	                                 ShaderFormat format, ShaderType type);

	/** Returns the shader binary format preferred by the running platform. */
	static ShaderFormat getSupportedFormat();

	/** Returns a human-readable name for a shader format enum value. */
	static std::string formatToString(ShaderFormat format);
	/** Returns a human-readable name for a shader type enum value. */
	static std::string typeToString(ShaderType type);

	/** Returns the conventional filename extension for a shader format. */
	static std::string getShaderExtension(ShaderFormat format);
};

} // namespace shiki
