#include <catch2/catch_test_macros.hpp>

#include <shiki/core/result.h>
#include <shiki/core/time.h>
#include <shiki/game/type_registry.h>

#include <string>

TEST_CASE("FNV-1a type keys are stable", "[core][registry]") {
	using shiki::game::typeKeyFromName;

	CHECK(typeKeyFromName("").value == 14695981039346656037ULL);
	CHECK(typeKeyFromName("a").value == 12638187200555641996ULL);
	CHECK(typeKeyFromName("foobar").value == 9625390261332436968ULL);
}

TEST_CASE("Type registry freeze is independent of registration order",
          "[core][registry]") {
	using namespace shiki::game;

	TypeRegistry first;
	REQUIRE(first.add({TypeDomain::Event, "game.hit.v1", 1, 2, "hit"}));
	REQUIRE(first.add(
	    {TypeDomain::Component, "game.transform.v1", 3, 1, "transform"}));

	TypeRegistry second;
	REQUIRE(second.add(
	    {TypeDomain::Component, "game.transform.v1", 3, 1, "transform"}));
	REQUIRE(second.add({TypeDomain::Event, "game.hit.v1", 1, 2, "hit"}));

	REQUIRE(first.freeze());
	REQUIRE(second.freeze());
	CHECK(first.digest() == second.digest());
	REQUIRE(first.entries().size() == second.entries().size());
	for (std::size_t index = 0; index < first.entries().size(); ++index) {
		CHECK(first.entries()[index].identity ==
		      second.entries()[index].identity);
		CHECK(first.entries()[index].index == second.entries()[index].index);
	}
}

TEST_CASE("Type registry rejects invalid lifecycle operations",
          "[core][registry]") {
	using namespace shiki::game;

	TypeRegistry registry;
	const auto empty = registry.add({TypeDomain::Component, ""});
	REQUIRE_FALSE(empty);
	CHECK(empty.error().code ==
	      static_cast<std::uint32_t>(TypeRegistryError::EmptyName));

	REQUIRE(registry.add({TypeDomain::Component, "game.transform.v1"}));
	const auto duplicate =
	    registry.add({TypeDomain::Component, "game.transform.v1"});
	REQUIRE_FALSE(duplicate);
	CHECK(duplicate.error().code ==
	      static_cast<std::uint32_t>(TypeRegistryError::DuplicateType));

	REQUIRE(registry.freeze());
	const auto late = registry.add({TypeDomain::Event, "game.hit.v1"});
	REQUIRE_FALSE(late);
	CHECK(late.error().code ==
	      static_cast<std::uint32_t>(TypeRegistryError::RegistryFrozen));
	REQUIRE_FALSE(registry.freeze());
}

TEST_CASE("Type registry separates domains and resolves dense indices",
          "[core][registry]") {
	using namespace shiki::game;

	TypeRegistry registry;
	REQUIRE(registry.add({TypeDomain::Component, "game.shared.v1"}));
	REQUIRE(registry.add({TypeDomain::Event, "game.shared.v1"}));
	REQUIRE(registry.freeze());

	const auto *component =
	    registry.find(TypeDomain::Component, "game.shared.v1");
	const auto *event = registry.find(TypeDomain::Event, "game.shared.v1");
	REQUIRE(component != nullptr);
	REQUIRE(event != nullptr);
	CHECK(component->identity.key == event->identity.key);
	CHECK(component->index != event->index);
	CHECK(registry.find(component->index) == component);
}

TEST_CASE("Tick types preserve fixed-step units", "[core][time]") {
	const shiki::Tick start{40};
	const shiki::TickSpan duration{20};
	const shiki::Tick end = start + duration;

	CHECK(end.value == 60);
	CHECK(end - start == 20);
	CHECK(shiki::TickRate{60}.isValid());
	CHECK_FALSE(shiki::TickRate{0}.isValid());
}

TEST_CASE("Legacy Result construction remains source compatible",
          "[core][result]") {
	shiki::Result<int> success = 7;
	shiki::Result<int> failure =
	    std::unexpected(shiki::Error{"legacy diagnostic"});

	REQUIRE(success);
	CHECK(*success == 7);
	REQUIRE_FALSE(failure);
	CHECK(failure.error().domain == shiki::ErrorDomain::Core);
	CHECK(failure.error().message == "legacy diagnostic");
}
