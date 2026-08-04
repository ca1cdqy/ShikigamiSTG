#include <cstring>
#include <fstream>
#include <shiki/ecl/ecl_parser.h>
#include <spdlog/spdlog.h>

namespace shiki {
namespace ecl {

struct TH06Header {
	uint16_t subCount;
	uint16_t timelineCount;
};

struct TH06Instr {
	uint32_t time;
	uint16_t id;
	uint16_t size;
	uint16_t rankMask;
	uint16_t paramMask;
};

// TH06 v1: time(2) + arg0(2) + id(2) + size(2) = 8 bytes
// TH08+ v2: time(4) + id(2) + size(1) + rank(1) = 8 bytes
struct TH06TimelineInstr {
	union {
		struct {
			int16_t timeV1;
			int16_t arg0;
		};
		int32_t timeV2;
	};
	uint16_t id;
	union {
		struct {
			uint8_t sizeV2;
			uint8_t rank;
		};
		int16_t sizeV1;
	};
};

struct FormatEntry {
	uint16_t id;
	const char *format;
};

static const FormatEntry th06Formats[] = {{0, ""},
                                          {1, "S"},
                                          {2, "to"},
                                          {3, "toS"},
                                          {4, "SS"},
                                          {5, "Sf"},
                                          {6, "SU"},
                                          {7, "SUS"},
                                          {8, "Sf"},
                                          {9, "Sff"},
                                          {10, "S"},
                                          {11, "S"},
                                          {12, "S"},
                                          {13, "SSS"},
                                          {14, "SSS"},
                                          {15, "SSS"},
                                          {16, "SSS"},
                                          {17, "SSS"},
                                          {18, "S"},
                                          {19, "S"},
                                          {20, "Sff"},
                                          {21, "Sff"},
                                          {22, "Sff"},
                                          {23, "Sff"},
                                          {24, "Sff"},
                                          {25, "Sffff"},
                                          {26, "S"},
                                          {27, "SS"},
                                          {28, "ff"},
                                          {29, "to"},
                                          {30, "to"},
                                          {31, "to"},
                                          {32, "to"},
                                          {33, "to"},
                                          {34, "to"},
                                          {35, "NSf"},
                                          {36, ""},
                                          {37, "NSfSS"},
                                          {38, "NSfSS"},
                                          {39, "NSfSS"},
                                          {40, "NSfSS"},
                                          {41, "NSfSS"},
                                          {42, "NSfSS"},
                                          {43, "fff"},
                                          {44, "fff"},
                                          {45, "ff"},
                                          {46, "f"},
                                          {47, "f"},
                                          {48, "f"},
                                          {49, "ff"},
                                          {50, "ff"},
                                          {51, "ff"},
                                          {52, "Sff"},
                                          {53, "Sff"},
                                          {54, "Sff"},
                                          {55, "Sff"},
                                          {56, "Sfff"},
                                          {57, "Sfff"},
                                          {58, "Sfff"},
                                          {59, "Sfff"},
                                          {60, "Sfff"},
                                          {61, "S"},
                                          {62, "S"},
                                          {63, "S"},
                                          {64, "S"},
                                          {65, "ffff"},
                                          {66, ""},
                                          {67, "ssSSffffS"},
                                          {68, "ssSSffffS"},
                                          {69, "ssSSffffS"},
                                          {70, "ssSSffffS"},
                                          {71, "ssSSffffS"},
                                          {72, "ssSSffffS"},
                                          {73, "ssSSffffS"},
                                          {74, "ssSSffffS"},
                                          {75, "ssSSffffS"},
                                          {76, "S"},
                                          {77, "S"},
                                          {78, ""},
                                          {79, ""},
                                          {80, ""},
                                          {81, "fff"},
                                          {82, "SSSSffff"},
                                          {83, ""},
                                          {84, "S"},
                                          {85, "ssffffffSSSSSS"},
                                          {86, "ssffffffSSSSSS"},
                                          {87, "S"},
                                          {88, "Sf"},
                                          {89, "Sf"},
                                          {90, "Sfff"},
                                          {91, "S"},
                                          {92, "S"},
                                          {93, "ssz"},
                                          {94, ""},
                                          {95, "NfffssS"},
                                          {96, ""},
                                          {97, "S"},
                                          {98, "ssssS"},
                                          {99, "SS"},
                                          {100, "S"},
                                          {101, "S"},
                                          {102, "Sffff"},
                                          {103, "fff"},
                                          {104, "S"},
                                          {105, "S"},
                                          {106, "S"},
                                          {107, "S"},
                                          {108, "N"},
                                          {109, "NS"},
                                          {110, "S"},
                                          {111, "S"},
                                          {112, "S"},
                                          {113, "S"},
                                          {114, "N"},
                                          {115, "S"},
                                          {116, "N"},
                                          {117, "S"},
                                          {118, "SUU"},
                                          {119, "S"},
                                          {120, "S"},
                                          {121, "SS"},
                                          {122, "S"},
                                          {123, "S"},
                                          {124, "S"},
                                          {125, ""},
                                          {126, "S"},
                                          {127, "S"},
                                          {128, "S"},
                                          {129, "SS"},
                                          {130, "S"},
                                          {131, "ffSSSS"},
                                          {132, "S"},
                                          {133, ""},
                                          {134, ""},
                                          {135, "S"},
                                          {0xFFFF, nullptr}};

static const FormatEntry th06TimelineFormats[] = {
    {0, "nfffssS"}, {1, "nfff"},      {2, "nfffssS"}, {3, "nfff"},
    {4, "nfffssS"}, {5, "nfff"},      {6, "nfffssS"}, {7, "nfff"},
    {8, "s"},       {9, ""},          {10, "SS"},     {11, "u"},
    {12, "s"},      {0xFFFF, nullptr}};

static const char *findFormat(const FormatEntry *table, uint16_t id) {
	for (size_t i = 0; table[i].format != nullptr; ++i) {
		if (table[i].id == id) {
			return table[i].format;
		}
	}
	return nullptr;
}

bool ECLParser::parseFile(const std::string &filePath, uint32_t version) {
	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open()) {
		spdlog::error("Failed to open ECL file: {}", filePath);
		return false;
	}

	file.seekg(0, std::ios::end);
	size_t fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(fileSize);
	file.read(reinterpret_cast<char *>(data.data()),
	          static_cast<std::streamsize>(fileSize));
	file.close();

	spdlog::info("Parsing ECL file: {} ({} bytes, version {})", filePath,
	             fileSize, version);
	return parse(std::as_bytes(std::span{data}), version).has_value();
}

Result<void> ECLParser::parse(std::span<const std::byte> bytes,
                              uint32_t version) {
	clear();
	file_.version = version;
	if (bytes.empty()) {
		return std::unexpected(
		    Error{ErrorDomain::Script, 1, "ECL source is empty"});
	}
	const auto *data = reinterpret_cast<const uint8_t *>(bytes.data());
	if (!parseHeader(data, bytes.size())) {
		clear();
		return std::unexpected(
		    Error{ErrorDomain::Script, 2, "ECL source is malformed"});
	}
	return {};
}

bool ECLParser::parseHeader(const uint8_t *data, size_t size) {
	if (size < sizeof(TH06Header)) {
		spdlog::error("ECL file too small for header");
		return false;
	}

	size_t headerOffset = 0;
	if (file_.version >= 8) {
		if (size < sizeof(uint32_t) + sizeof(TH06Header)) {
			spdlog::error("ECL file too small for header with magic");
			return false;
		}
		uint32_t magic = *reinterpret_cast<const uint32_t *>(data);
		if (magic != 0x00000800 && magic != 0x00000900) {
			spdlog::warn("Unexpected ECL magic: 0x{:08x}", magic);
		}
		headerOffset = sizeof(uint32_t);
	}

	const TH06Header *header =
	    reinterpret_cast<const TH06Header *>(data + headerOffset);
	uint16_t subCount = header->subCount;
	uint16_t timelineCount = header->timelineCount;

	spdlog::info("ECL: {} subroutines, {} timelines", subCount, timelineCount);

	size_t timelineCountMax;
	if (file_.version == 9) {
		timelineCountMax = timelineCount;
	} else if (file_.version == 6) {
		timelineCountMax = 3;
	} else {
		timelineCountMax = 16;
	}

	size_t offsetCount = timelineCountMax + subCount;
	size_t offsetsSize = sizeof(uint32_t) * offsetCount;
	size_t headerSize = headerOffset + sizeof(TH06Header) + offsetsSize;

	if (size < headerSize) {
		spdlog::error("ECL file too small for offset table");
		return false;
	}

	std::vector<uint32_t> offsets(offsetCount);
	const uint32_t *offsetData = reinterpret_cast<const uint32_t *>(
	    data + headerOffset + sizeof(TH06Header));
	for (size_t i = 0; i < offsetCount; ++i) {
		offsets[i] = offsetData[i];
	}

	if (!parseSubroutines(data, size, offsets, timelineCountMax, subCount)) {
		spdlog::error("Failed to parse subroutines");
		return false;
	}

	const size_t actualTimelineCount = (file_.version == 6) ? 1 : timelineCount;

	if (!parseTimelines(data, size, offsets, actualTimelineCount)) {
		spdlog::error("Failed to parse timelines");
		return false;
	}

	spdlog::info("ECL parsing complete: {} subs, {} timelines",
	             file_.subs.size(), file_.timelines.size());
	return true;
}

bool ECLParser::parseSubroutines(const uint8_t *data, size_t size,
                                 const std::vector<uint32_t> &offsets,
                                 size_t timelineCountMax, size_t subCount) {
	for (size_t s = 0; s < subCount; ++s) {
		ECLSubroutine sub;
		sub.name = "Sub" + std::to_string(s);

		size_t subOffset = offsets[timelineCountMax + s];
		if (subOffset >= size) {
			spdlog::warn("Subroutine {} offset {} exceeds file size {}", s,
			             subOffset, size);
			continue;
		}

		size_t offset = subOffset;
		while (offset + sizeof(TH06Instr) <= size) {
			const TH06Instr *rawInstr =
			    reinterpret_cast<const TH06Instr *>(data + offset);

			if (rawInstr->time == 0xFFFFFFFF || rawInstr->size == 0) {
				break;
			}

			if (offset + rawInstr->size > size) {
				spdlog::warn(
				    "Instruction size exceeds file bounds at offset {}",
				    offset);
				break;
			}

			ECLInstruction instr;
			instr.time = rawInstr->time;
			instr.id = rawInstr->id;
			instr.size = rawInstr->size;
			instr.rankMask = rawInstr->rankMask;
			instr.paramMask = rawInstr->paramMask;
			instr.address = static_cast<uint32_t>(offset);

			size_t paramDataSize = rawInstr->size - sizeof(TH06Instr);
			if (paramDataSize > 0) {
				std::string format = getInstructionFormat(rawInstr->id, false);
				if (!format.empty()) {
					bool ok = parseParams(data + offset + sizeof(TH06Instr),
					                      paramDataSize, rawInstr->id, format,
					                      instr.params);
					if (!ok) {
						instr.params.clear();
					}
				}
			}

			sub.instructions.push_back(instr);
			offset += rawInstr->size;
		}

		file_.subs.push_back(std::move(sub));
		spdlog::debug("Parsed subroutine {}: {} instructions", s,
		              file_.subs.back().instructions.size());
	}

	return true;
}

bool ECLParser::parseTimelines(const uint8_t *data, size_t size,
                               const std::vector<uint32_t> &offsets,
                               size_t timelineCount) {
	bool isTimelineV2 = file_.version >= 8;

	for (size_t t = 0; t < timelineCount; ++t) {
		ECLTimeline timeline;
		timeline.name = "Timeline" + std::to_string(t);

		size_t timelineOffset = offsets[t];
		if (timelineOffset >= size) {
			spdlog::warn("Timeline {} offset {} exceeds file size {}", t,
			             timelineOffset, size);
			continue;
		}

		size_t offset = timelineOffset;
		size_t timelineInstrSize =
		    8; // TH06 v1 and TH08+ v2 both use 8 byte header
		while (offset + timelineInstrSize <= size) {
			const TH06TimelineInstr *rawInstr =
			    reinterpret_cast<const TH06TimelineInstr *>(data + offset);

			bool isEnd;
			if (isTimelineV2) {
				isEnd = rawInstr->timeV2 == -1 && rawInstr->sizeV2 == 0x00;
			} else {
				isEnd = rawInstr->timeV1 == -1 && rawInstr->arg0 == 4;
			}

			if (isEnd) {
				break;
			}

			int32_t insTime =
			    isTimelineV2 ? rawInstr->timeV2 : rawInstr->timeV1;
			int32_t insSize =
			    isTimelineV2 ? rawInstr->sizeV2 : rawInstr->sizeV1;
			uint16_t insId = rawInstr->id;
			uint16_t insRankMask = isTimelineV2 ? rawInstr->rank : 0xFF;

			if (insSize <= 0) {
				spdlog::warn("Invalid timeline instruction size at offset {}",
				             offset);
				break;
			}

			if (offset + insSize > size) {
				spdlog::warn("Timeline instruction size exceeds file bounds at "
				             "offset {}",
				             offset);
				break;
			}

			ECLInstruction instr;
			instr.time = static_cast<uint32_t>(insTime);
			instr.id = insId;
			instr.size = static_cast<uint16_t>(insSize);
			instr.rankMask = insRankMask;
			instr.address = static_cast<uint32_t>(offset);

			size_t paramDataSize = insSize - timelineInstrSize;
			size_t paramDataOffset = offset + timelineInstrSize;

			std::string format = getInstructionFormat(insId, true);
			if (!format.empty()) {
				bool ok = true;
				if (!isTimelineV2) {
					char firstType = format[0];
					if (firstType == 'n' || firstType == 's' ||
					    firstType == 'u' || firstType == 'b' ||
					    firstType == 'c') {
						ECLParam param;
						param.type = firstType;
						param.value = static_cast<int32_t>(rawInstr->arg0);
						instr.params.push_back(param);
						format = format.substr(1);
					}
				}

				if (!format.empty()) {
					ok = paramDataSize > 0 &&
					     parseParams(data + paramDataOffset, paramDataSize,
					                 insId, format, instr.params);
				}
				if (!ok) {
					instr.params.clear();
				}
			}

			timeline.instructions.push_back(instr);
			offset += insSize;
		}

		file_.timelines.push_back(std::move(timeline));
		spdlog::debug("Parsed timeline {}: {} instructions", t,
		              file_.timelines.back().instructions.size());
	}

	return true;
}

bool ECLParser::parseParams(const uint8_t *data, size_t dataSize, uint16_t id,
                            const std::string &format,
                            std::vector<ECLParam> &params) {
	size_t offset = 0;

	for (char type : format) {
		if (offset >= dataSize) {
			break;
		}

		ECLParam param;
		param.type = type;

		switch (type) {
		case 'S': { // int32 (thtk compatible)
			if (offset + sizeof(int32_t) > dataSize)
				return false;
			int32_t value = *reinterpret_cast<const int32_t *>(data + offset);
			param.value = value;
			offset += sizeof(int32_t);
			break;
		}
		case 's': { // int16 (thtk compatible)
			if (offset + sizeof(int16_t) > dataSize)
				return false;
			int16_t value = *reinterpret_cast<const int16_t *>(data + offset);
			param.value = static_cast<int32_t>(value);
			offset += sizeof(int16_t);
			break;
		}
		case 'U': { // uint32 (thtk compatible)
			if (offset + sizeof(uint32_t) > dataSize)
				return false;
			uint32_t value = *reinterpret_cast<const uint32_t *>(data + offset);
			param.value = value;
			offset += sizeof(uint32_t);
			break;
		}
		case 'u': { // uint16 (thtk compatible)
			if (offset + sizeof(uint16_t) > dataSize)
				return false;
			uint16_t value = *reinterpret_cast<const uint16_t *>(data + offset);
			param.value = static_cast<uint32_t>(value);
			offset += sizeof(uint16_t);
			break;
		}
		case 'f': { // float
			if (offset + sizeof(float) > dataSize)
				return false;
			float value = *reinterpret_cast<const float *>(data + offset);
			param.value = value;
			offset += sizeof(float);
			break;
		}
		case 'C': { // uint8 (color?)
			if (offset + sizeof(uint8_t) > dataSize)
				return false;
			uint8_t value = *reinterpret_cast<const uint8_t *>(data + offset);
			param.value = static_cast<uint32_t>(value);
			offset += sizeof(uint8_t);
			break;
		}
		case 'z': { // string (TH06: 34 bytes, TH07/95: 48 bytes)
			size_t strLen = 34;
			if (file_.version == 7 || file_.version == 95) {
				strLen = 48;
			}
			if (offset + strLen > dataSize)
				return false;
			param.value = std::string(
			    reinterpret_cast<const char *>(data + offset), strLen);
			offset += strLen;
			break;
		}
		case 'n':   // sub name (int16)
		case 'N':   // sub name (int32)
		case 'o':   // offset (int32)
		case 't':   // time (int32)
		case 'T': { // timeline name (int32)
			if (type == 'n') {
				if (offset + sizeof(int16_t) > dataSize)
					return false;
				int16_t value =
				    *reinterpret_cast<const int16_t *>(data + offset);
				param.value = static_cast<int32_t>(value);
				offset += sizeof(int16_t);
			} else {
				if (offset + sizeof(int32_t) > dataSize)
					return false;
				int32_t value =
				    *reinterpret_cast<const int32_t *>(data + offset);
				param.value = value;
				offset += sizeof(int32_t);
			}
			break;
		}
		default:
			spdlog::warn("Unknown parameter type '{}' at instruction {}", type,
			             id);
			return false;
		}

		params.push_back(param);
	}

	return true;
}

std::string ECLParser::getInstructionFormat(uint16_t id,
                                            bool isTimeline) const {
	if (isTimeline) {
		const char *format = findFormat(th06TimelineFormats, id);
		if (format)
			return format;
	} else {
		const char *format = findFormat(th06Formats, id);
		if (format)
			return format;
	}

	spdlog::debug("Unknown instruction ID: {} (timeline={})", id, isTimeline);
	return "";
}

const ECLSubroutine *ECLParser::getSubroutine(const std::string &name) const {
	for (const auto &sub : file_.subs) {
		if (sub.name == name) {
			return &sub;
		}
	}
	return nullptr;
}

const ECLSubroutine *ECLParser::getSubroutine(size_t index) const {
	if (index < file_.subs.size()) {
		return &file_.subs[index];
	}
	return nullptr;
}

const ECLTimeline *ECLParser::getTimeline(const std::string &name) const {
	for (const auto &timeline : file_.timelines) {
		if (timeline.name == name) {
			return &timeline;
		}
	}
	return nullptr;
}

const ECLTimeline *ECLParser::getTimeline(size_t index) const {
	if (index < file_.timelines.size()) {
		return &file_.timelines[index];
	}
	return nullptr;
}

void ECLParser::clear() {
	file_.subs.clear();
	file_.timelines.clear();
}

} // namespace ecl
} // namespace shiki
