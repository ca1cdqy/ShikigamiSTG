#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace shiki {

/** Identifies the subsystem that produced an error. */
enum class ErrorDomain : std::uint16_t {
	Core,         ///< Core runtime and general contract errors.
	Definition,   ///< Game definition and registration errors.
	World,        ///< Entity and world errors.
	Schedule,     ///< System scheduling errors.
	Asset,        ///< Asset loading and decoding errors.
	Flow,         ///< Stage and flow errors.
	Script,       ///< Script parsing and execution errors.
	Presentation, ///< Rendering, UI, and presentation errors.
	Platform      ///< Window, audio, and platform service errors.
};

/** A structured field attached to an error diagnostic. */
struct ErrorField final {
	std::string name;
	std::string value;
};

/** Describes a recoverable engine error without using exceptions. */
struct Error final {
	ErrorDomain domain{ErrorDomain::Core};
	std::uint32_t code{};
	std::string message;
	std::vector<ErrorField> fields;

	/** Creates a legacy core error from a diagnostic message. */
	explicit Error(std::string diagnostic) : message(std::move(diagnostic)) {}

	/** Creates a structured error. */
	Error(ErrorDomain errorDomain, std::uint32_t errorCode,
	      std::string diagnostic, std::vector<ErrorField> context = {})
	    : domain(errorDomain), code(errorCode), message(std::move(diagnostic)),
	      fields(std::move(context)) {}
};

/** The public result type used for recoverable failures. */
template <class T> using Result = std::expected<T, Error>;

} // namespace shiki
