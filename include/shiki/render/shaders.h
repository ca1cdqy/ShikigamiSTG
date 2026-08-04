#pragma once

/// SDL3 GPU shader bytecode
/// Windows uses DXBC bytecode

/**
 * @file
 * @brief Embedded shader bytecode declarations.
 *
 * Production builds should compile vertex and fragment shaders externally
 * with DXC or glslc and load the resulting files at runtime. This header
 * provides only a compile-time flag to detect whether embedded bytecode is
 * available.
 *
 * To embed shaders: add the compiled bytecode arrays here and flip
 * USE_PRECOMPILED_SHADERS to true.
 */

namespace shiki {
namespace shaders {

/** False when shaders are loaded from external files at runtime;
 *  true when production bytecode arrays are embedded in this header. */
[[maybe_unused]] constexpr bool USE_PRECOMPILED_SHADERS = false;

/// When USE_PRECOMPILED_SHADERS is true, uncomment and populate these:
// extern const unsigned char vertexShaderBytecode[];
// extern const size_t vertexShaderBytecodeSize;
// extern const unsigned char fragmentShaderBytecode[];
// extern const size_t fragmentShaderBytecodeSize;

} // namespace shaders
} // namespace shiki
