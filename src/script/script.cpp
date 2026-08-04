#include <shiki/script/script.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace shiki {


bool ScriptParser::parse(const std::string& script) {
    std::istringstream stream(script);
    std::string line;
    ScriptSection currentSection;
    currentSection.name = "default";

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            if (!currentSection.commands.empty()) {
                sections_.push_back(currentSection);
            }
            currentSection = ScriptSection();
            currentSection.name = line.substr(1, line.length() - 2);
            continue;
        }

        ScriptCommand command;
        if (parseLine(line, command)) {
            currentSection.commands.push_back(command);
        }
    }

    if (!currentSection.commands.empty()) {
        sections_.push_back(currentSection);
    }

    return true;
}

bool ScriptParser::parseFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse(buffer.str());
}

ScriptValue ScriptParser::getVariable(const std::string& name) const {
    auto it = variables_.find(name);
    if (it != variables_.end()) {
        return it->second;
    }
    return ScriptValue();
}

void ScriptParser::setVariable(const std::string& name, const ScriptValue& value) {
    variables_[name] = value;
}

void ScriptParser::clear() {
    sections_.clear();
    variables_.clear();
}

bool ScriptParser::parseLine(const std::string& line, ScriptCommand& command) {
    std::vector<std::string> tokens = split(line, ' ');
    if (tokens.empty()) return false;

    command.name = tokens[0];

    for (size_t i = 1; i < tokens.size(); ++i) {
        size_t pos = tokens[i].find('=');
        if (pos != std::string::npos) {
            std::string key = tokens[i].substr(0, pos);
            std::string value = tokens[i].substr(pos + 1);
            command.parameters[key] = parseValue(value);
        }
    }

    return true;
}

ScriptValue ScriptParser::parseValue(const std::string& valueStr) {
    std::string str = trim(valueStr);

    try {
        size_t pos = 0;
        int intValue = std::stoi(str, &pos);
        if (pos == str.length()) {
            return ScriptValue(intValue);
        }
    } catch (...) {
    }

    try {
        size_t pos = 0;
        float floatValue = std::stof(str, &pos);
        if (pos == str.length()) {
            return ScriptValue(floatValue);
        }
    } catch (...) {
    }

    if (str == "true" || str == "True") {
        return ScriptValue(true);
    }
    if (str == "false" || str == "False") {
        return ScriptValue(false);
    }

    if (str[0] == '(' && str.back() == ')') {
        std::string inner = str.substr(1, str.length() - 2);
        std::vector<std::string> coords = split(inner, ',');
        if (coords.size() == 2) {
            float x = std::stof(trim(coords[0]));
            float y = std::stof(trim(coords[1]));
            return ScriptValue(Vec2(x, y));
        }
    }

    if (str[0] == '#' && str.length() == 9) {
        uint32_t color = std::stoul(str.substr(1), nullptr, 16);
        return ScriptValue(color);
    }

    return ScriptValue(str);
}

std::string ScriptParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::vector<std::string> ScriptParser::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        token = trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}


bool ScriptExecutor::loadScript(const std::string& script) {
    if (!parser_.parse(script)) {
        return false;
    }
    restart();
    return true;
}

bool ScriptExecutor::loadScriptFile(const std::string& filePath) {
    if (!parser_.parseFile(filePath)) {
        return false;
    }
    restart();
    return true;
}

void ScriptExecutor::update(float dt) {
    if (!isRunning_ || isCompleted_) return;

    currentTime_ += dt;

    const auto& sections = parser_.getSections();
    if (currentSection_ >= sections.size()) {
        isCompleted_ = true;
        return;
    }

    const auto& section = sections[currentSection_];
    if (currentCommand_ < section.commands.size()) {
        const auto& command = section.commands[currentCommand_];

        if (currentTime_ >= command.delay) {
            executeCommand(command);
            currentCommand_++;
        }
    } else {
        currentSection_++;
        currentCommand_ = 0;
        currentTime_ = 0.0f;
    }
}

void ScriptExecutor::start() {
    isRunning_ = true;
    isCompleted_ = false;
}

void ScriptExecutor::stop() {
    isRunning_ = false;
}

void ScriptExecutor::restart() {
    currentTime_ = 0.0f;
    currentSection_ = 0;
    currentCommand_ = 0;
    isRunning_ = false;
    isCompleted_ = false;
}

void ScriptExecutor::executeCommand(const ScriptCommand& command) {
    if (commandCallback_) {
        commandCallback_(command);
    }
}


void ScriptSystem::initialize() {
    clear();
}

void ScriptSystem::shutdown() {
    clear();
}

bool ScriptSystem::loadScript(const std::string& name, const std::string& script) {
    auto executor = std::make_unique<ScriptExecutor>();
    if (!executor->loadScript(script)) {
        return false;
    }
    executors_[name] = std::move(executor);
    return true;
}

bool ScriptSystem::loadScriptFile(const std::string& name, const std::string& filePath) {
    auto executor = std::make_unique<ScriptExecutor>();
    if (!executor->loadScriptFile(filePath)) {
        return false;
    }
    executors_[name] = std::move(executor);
    return true;
}

void ScriptSystem::execute(const std::string& name) {
    auto* executor = getExecutor(name);
    if (executor) {
        executor->start();
    }
}

void ScriptSystem::update(float dt) {
    for (auto& pair : executors_) {
        if (pair.second->isRunning()) {
            pair.second->update(dt);
        }
    }
}

ScriptExecutor* ScriptSystem::getExecutor(const std::string& name) {
    auto it = executors_.find(name);
    if (it != executors_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ScriptSystem::clear() {
    executors_.clear();
}

} // namespace shiki
