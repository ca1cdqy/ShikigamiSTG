#include <shiki/runtime.h>

namespace shiki {

Result<std::unique_ptr<Session>>
Runtime::createSession(GameDefinition definition, SessionConfig config) {
	return Session::create(std::move(definition), config);
}

} // namespace shiki
