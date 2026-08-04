#include <shiki/ecl/ecl_stage_parser.h>

#include <shiki/core/binary_codec.h>
#include <shiki/ecl/ecl_parser.h>

#include <array>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace shiki::ecl {
namespace {

[[nodiscard]] Error makeError(EclStageError code, const char *message) {
	return Error{ErrorDomain::Script, static_cast<std::uint32_t>(code),
	             message};
}

template <class T>
[[nodiscard]] Result<T> finishDecode(BinaryReader &reader, T value) {
	if (!reader.empty())
		return std::unexpected(
		    Error{ErrorDomain::Script, 100, "ECL payload has trailing bytes"});
	return value;
}

[[nodiscard]] flow::ActionCodec<EclSpawnEnemyAction> spawnCodec() {
	return {.encode = [](const EclSpawnEnemyAction &value)
	            -> Result<std::vector<std::byte>> {
		        BinaryWriter writer;
		        writer.writeU32(value.opcode);
		        writer.writeU32(std::bit_cast<std::uint32_t>(value.subroutine));
		        writer.writeF32(value.x);
		        writer.writeF32(value.y);
		        writer.writeF32(value.z);
		        writer.writeU32(std::bit_cast<std::uint32_t>(value.life));
		        writer.writeU32(std::bit_cast<std::uint32_t>(value.itemDrop));
		        writer.writeU32(std::bit_cast<std::uint32_t>(value.score));
		        return std::move(writer).finish();
	        },
	        .decode = [](std::span<const std::byte> bytes)
	            -> Result<EclSpawnEnemyAction> {
		        BinaryReader reader(bytes);
		        auto opcode = reader.readU32();
		        auto subroutine = reader.readU32();
		        auto x = reader.readF32();
		        auto y = reader.readF32();
		        auto z = reader.readF32();
		        auto life = reader.readU32();
		        auto itemDrop = reader.readU32();
		        auto score = reader.readU32();
		        if (!opcode || !subroutine || !x || !y || !z || !life ||
		            !itemDrop || !score)
			        return std::unexpected(
			            makeError(EclStageError::InvalidParameters,
			                      "ECL enemy spawn payload is truncated"));
		        return finishDecode(
		            reader,
		            EclSpawnEnemyAction{
		                *opcode, std::bit_cast<std::int32_t>(*subroutine), *x,
		                *y, *z, std::bit_cast<std::int32_t>(*life),
		                std::bit_cast<std::int32_t>(*itemDrop),
		                std::bit_cast<std::int32_t>(*score)});
	        }};
}

template <class T> [[nodiscard]] flow::ActionCodec<T> emptyCodec() {
	return {.encode = [](const T &) -> Result<std::vector<std::byte>> {
		        return {};
	        },
	        .decode = [](std::span<const std::byte> bytes) -> Result<T> {
		        if (!bytes.empty())
			        return std::unexpected(
			            Error{ErrorDomain::Script, 101,
			                  "Empty ECL payload is not empty"});
		        return T{};
	        }};
}

template <class T> [[nodiscard]] flow::ActionCodec<T> oneI32Codec() {
	return {.encode = [](const T &value) -> Result<std::vector<std::byte>> {
		        BinaryWriter writer;
		        const std::int32_t encoded = [&] {
			        if constexpr (std::same_as<T, EclStartDialogueAction>)
				        return value.messageId;
			        else
				        return value.bossId;
		        }();
		        writer.writeU32(std::bit_cast<std::uint32_t>(encoded));
		        return std::move(writer).finish();
	        },
	        .decode = [](std::span<const std::byte> bytes) -> Result<T> {
		        BinaryReader reader(bytes);
		        auto value = reader.readU32();
		        if (!value)
			        return std::unexpected(value.error());
		        return finishDecode(reader,
		                            T{std::bit_cast<std::int32_t>(*value)});
	        }};
}

[[nodiscard]] flow::ActionCodec<EclSetPlayerPowerAction> powerCodec() {
	return {.encode = [](const EclSetPlayerPowerAction &value)
	            -> Result<std::vector<std::byte>> {
		        BinaryWriter writer;
		        writer.writeU32(value.value);
		        return std::move(writer).finish();
	        },
	        .decode = [](std::span<const std::byte> bytes)
	            -> Result<EclSetPlayerPowerAction> {
		        BinaryReader reader(bytes);
		        auto value = reader.readU32();
		        if (!value)
			        return std::unexpected(value.error());
		        return finishDecode(reader, EclSetPlayerPowerAction{*value});
	        }};
}

[[nodiscard]] flow::ActionCodec<EclBossInterruptCondition> interruptCodec() {
	return {
	    .encode = [](const EclBossInterruptCondition &value)
	        -> Result<std::vector<std::byte>> {
		    BinaryWriter writer;
		    writer.writeU32(std::bit_cast<std::uint32_t>(value.bossId));
		    writer.writeU32(std::bit_cast<std::uint32_t>(value.interruptId));
		    return std::move(writer).finish();
	    },
	    .decode = [](std::span<const std::byte> bytes)
	        -> Result<EclBossInterruptCondition> {
		    BinaryReader reader(bytes);
		    auto boss = reader.readU32();
		    auto interrupt = reader.readU32();
		    if (!boss || !interrupt)
			    return std::unexpected(
			        makeError(EclStageError::InvalidParameters,
			                  "ECL boss interrupt payload is truncated"));
		    return finishDecode(reader,
		                        EclBossInterruptCondition{
		                            std::bit_cast<std::int32_t>(*boss),
		                            std::bit_cast<std::int32_t>(*interrupt)});
	    }};
}

[[nodiscard]] Result<std::int32_t>
signedParam(const ECLInstruction &instruction, std::size_t index) {
	if (index >= instruction.params.size())
		return std::unexpected(makeError(EclStageError::InvalidParameters,
		                                 "ECL integer parameter is missing"));
	const ECLValue &value = instruction.params[index].value;
	if (const auto *signedValue = std::get_if<std::int32_t>(&value))
		return *signedValue;
	if (const auto *unsignedValue = std::get_if<std::uint32_t>(&value)) {
		if (*unsignedValue > static_cast<std::uint32_t>(
		                         std::numeric_limits<std::int32_t>::max()))
			return std::unexpected(
			    makeError(EclStageError::InvalidParameters,
			              "ECL integer parameter is outside the signed range"));
		return static_cast<std::int32_t>(*unsignedValue);
	}
	return std::unexpected(makeError(EclStageError::InvalidParameters,
	                                 "ECL parameter is not an integer"));
}

[[nodiscard]] Result<float> floatParam(const ECLInstruction &instruction,
                                       std::size_t index) {
	if (index >= instruction.params.size())
		return std::unexpected(makeError(EclStageError::InvalidParameters,
		                                 "ECL float parameter is missing"));
	if (const auto *value =
	        std::get_if<float>(&instruction.params[index].value))
		return *value;
	return std::unexpected(makeError(EclStageError::InvalidParameters,
	                                 "ECL parameter is not a float"));
}

} // namespace

Result<EclStageTokens>
registerEclStageCompatibility(flow::ActionRegistry &actions,
                              flow::ConditionRegistry &conditions,
                              EclStageHandlers handlers) {
	if (!handlers.spawnEnemy || !handlers.startDialogue ||
	    !handlers.setPlayerPower || !handlers.dialogueComplete ||
	    !handlers.bossInterruptAccepted || !handlers.bossDefeated)
		return std::unexpected(
		    makeError(EclStageError::InvalidHandlers,
		              "All ECL stage handlers are required"));
	auto spawn = actions.add<EclSpawnEnemyAction>(
	    "shiki.compat.th06.ecl.spawn_enemy", 1, spawnCodec(),
	    std::move(handlers.spawnEnemy));
	if (!spawn)
		return std::unexpected(spawn.error());
	auto dialogue = actions.add<EclStartDialogueAction>(
	    "shiki.compat.th06.ecl.start_dialogue", 1,
	    oneI32Codec<EclStartDialogueAction>(),
	    std::move(handlers.startDialogue));
	if (!dialogue)
		return std::unexpected(dialogue.error());
	auto power = actions.add<EclSetPlayerPowerAction>(
	    "shiki.compat.th06.ecl.set_player_power", 1, powerCodec(),
	    std::move(handlers.setPlayerPower));
	if (!power)
		return std::unexpected(power.error());
	auto dialogueDone = conditions.add<EclDialogueCompleteCondition>(
	    "shiki.compat.th06.ecl.dialogue_complete", 1,
	    emptyCodec<EclDialogueCompleteCondition>(),
	    std::move(handlers.dialogueComplete));
	if (!dialogueDone)
		return std::unexpected(dialogueDone.error());
	auto interrupt = conditions.add<EclBossInterruptCondition>(
	    "shiki.compat.th06.ecl.boss_interrupt_accepted", 1, interruptCodec(),
	    std::move(handlers.bossInterruptAccepted));
	if (!interrupt)
		return std::unexpected(interrupt.error());
	auto defeated = conditions.add<EclBossDefeatedCondition>(
	    "shiki.compat.th06.ecl.boss_defeated", 1,
	    oneI32Codec<EclBossDefeatedCondition>(),
	    std::move(handlers.bossDefeated));
	if (!defeated)
		return std::unexpected(defeated.error());
	return EclStageTokens{*spawn,        *dialogue,  *power,
	                      *dialogueDone, *interrupt, *defeated};
}

Result<flow::StageProgram>
EclStageParser::parse(const flow::StageSource &source,
                      const flow::StageParseContext &context) {
	ECLParser parser;
	auto parsed = parser.parse(source.bytes, 6);
	if (!parsed)
		return std::unexpected(parsed.error());
	return compileEclStage(parser.getFile(), tokens_, context);
}

Result<flow::StageProgram>
compileEclStage(const ECLFile &file, EclStageTokens tokens,
                const flow::StageParseContext &context) {
	const ECLTimeline *timeline =
	    file.timelines.empty() ? nullptr : &file.timelines.front();
	if (timeline == nullptr)
		return std::unexpected(makeError(EclStageError::MissingTimeline,
		                                 "TH06 ECL timeline zero is missing"));
	if (timeline->instructions.empty())
		return std::unexpected(makeError(EclStageError::EmptyTimeline,
		                                 "TH06 ECL timeline zero is empty"));

	flow::StageProgramBuilder builder;
	std::vector<flow::NodeId> nodes;
	std::uint32_t previousTime{};
	for (const ECLInstruction &instruction : timeline->instructions) {
		if (instruction.time < previousTime)
			return std::unexpected(
			    makeError(EclStageError::NonMonotonicTime,
			              "TH06 ECL timeline times are not monotonic"));
		const std::uint32_t delta = instruction.time - previousTime;
		if (delta != 0)
			nodes.push_back(builder.wait(TickSpan{delta}));
		previousTime = instruction.time;

		if (instruction.id <= 7) {
			auto subroutine = signedParam(instruction, 0);
			auto x = floatParam(instruction, 1);
			auto y = floatParam(instruction, 2);
			auto z = floatParam(instruction, 3);
			if (!subroutine || !x || !y || !z)
				return std::unexpected(
				    !subroutine
				        ? subroutine.error()
				        : (!x ? x.error() : (!y ? y.error() : z.error())));
			EclSpawnEnemyAction payload{instruction.id, *subroutine, *x, *y,
			                            *z};
			if ((instruction.id & 1U) == 0) {
				auto life = signedParam(instruction, 4);
				auto itemDrop = signedParam(instruction, 5);
				auto score = signedParam(instruction, 6);
				if (!life || !itemDrop || !score)
					return std::unexpected(
					    !life ? life.error()
					          : (!itemDrop ? itemDrop.error() : score.error()));
				payload.life = *life;
				payload.itemDrop = *itemDrop;
				payload.score = *score;
			}
			auto action = context.actions.make(tokens.spawnEnemy, payload);
			if (!action)
				return std::unexpected(action.error());
			nodes.push_back(builder.action(std::move(*action)));
			continue;
		}
		if (instruction.id == 8) {
			auto message = signedParam(instruction, 0);
			if (!message)
				return std::unexpected(message.error());
			auto action = context.actions.make(
			    tokens.startDialogue, EclStartDialogueAction{*message});
			if (!action)
				return std::unexpected(action.error());
			nodes.push_back(builder.action(std::move(*action)));
			continue;
		}
		if (instruction.id == 9) {
			auto condition = context.conditions.make(
			    tokens.dialogueComplete, EclDialogueCompleteCondition{});
			if (!condition)
				return std::unexpected(condition.error());
			nodes.push_back(builder.wait(std::move(*condition)));
			continue;
		}
		if (instruction.id == 10) {
			auto boss = signedParam(instruction, 0);
			auto interrupt = signedParam(instruction, 1);
			if (!boss || !interrupt)
				return std::unexpected(!boss ? boss.error()
				                             : interrupt.error());
			auto condition = context.conditions.make(
			    tokens.bossInterruptAccepted,
			    EclBossInterruptCondition{*boss, *interrupt});
			if (!condition)
				return std::unexpected(condition.error());
			nodes.push_back(builder.wait(std::move(*condition)));
			continue;
		}
		if (instruction.id == 11) {
			auto power = signedParam(instruction, 0);
			if (!power || *power < 0)
				return std::unexpected(
				    power ? makeError(EclStageError::InvalidParameters,
				                      "TH06 player power cannot be negative")
				          : power.error());
			auto action = context.actions.make(
			    tokens.setPlayerPower,
			    EclSetPlayerPowerAction{static_cast<std::uint32_t>(*power)});
			if (!action)
				return std::unexpected(action.error());
			nodes.push_back(builder.action(std::move(*action)));
			continue;
		}
		if (instruction.id == 12) {
			auto boss = signedParam(instruction, 0);
			if (!boss)
				return std::unexpected(boss.error());
			auto condition = context.conditions.make(
			    tokens.bossDefeated, EclBossDefeatedCondition{*boss});
			if (!condition)
				return std::unexpected(condition.error());
			nodes.push_back(builder.wait(std::move(*condition)));
			continue;
		}
		return std::unexpected(
		    Error{ErrorDomain::Script,
		          static_cast<std::uint32_t>(EclStageError::UnsupportedOpcode),
		          "TH06 ECL timeline opcode is unsupported",
		          {{"opcode", std::to_string(instruction.id)},
		           {"address", std::to_string(instruction.address)}}});
	}
	auto root = builder.sequence(nodes);
	if (!root)
		return std::unexpected(root.error());
	return std::move(builder).build(*root);
}

} // namespace shiki::ecl
