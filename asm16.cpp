#include "asm16_core.hpp"

#include <cctype>
#include <iomanip>
#include <map>
#include <optional>
#include <regex>

using namespace asm16;

namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string upper(std::string value) { for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return value; }
std::optional<uint16_t> number(const std::string& text) {
    try { size_t consumed = 0; unsigned long value = std::stoul(text, &consumed, 0); if (consumed == text.size() && value <= 0xffff) return static_cast<uint16_t>(value); } catch (...) {}
    return std::nullopt;
}
std::optional<uint8_t> reg(const std::string& name) {
    const std::string n = upper(trim(name));
    if (n == "AX") return 0;
    if (n == "BX") return 1;
    if (n == "CX") return 2;
    if (n == "DX") return 3;
    return std::nullopt;
}
struct Pending { std::string op, a, b; int line; };
bool assemble(const fs::path& input, std::vector<Instruction>& code, std::string& error) {
    std::ifstream file(input); if (!file) { error = "cannot open source: " + input.string(); return false; }
    std::vector<Pending> source; std::map<std::string, uint16_t> labels; std::string line; int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber; line = trim(line.substr(0, line.find(';'))); if (line.empty()) continue;
        const size_t colon = line.find(':');
        if (colon != std::string::npos) { const std::string label = upper(trim(line.substr(0, colon))); if (label.empty() || labels.count(label)) { error = "line " + std::to_string(lineNumber) + ": invalid or duplicate label"; return false; } labels[label] = source.size(); line = trim(line.substr(colon + 1)); if (line.empty()) continue; }
        std::istringstream words(line); Pending p; p.line = lineNumber; words >> p.op; p.op = upper(p.op); std::string operands; std::getline(words, operands); operands = trim(operands);
        const size_t comma = operands.find(','); p.a = trim(operands.substr(0, comma)); p.b = comma == std::string::npos ? "" : trim(operands.substr(comma + 1)); source.push_back(p);
    }
    for (const Pending& p : source) {
        Instruction i; const auto r = reg(p.a); auto valueOrLabel = [&](const std::string& text) -> std::optional<uint16_t> { if (auto value = number(text)) return value; const auto it = labels.find(upper(text)); return it == labels.end() ? std::nullopt : std::optional<uint16_t>(it->second); };
        if (p.op == "HLT" && p.a.empty()) i.op = Op::Hlt;
        else if (p.op == "NOP" && p.a.empty()) i.op = Op::Nop;
        else if ((p.op == "JMP" || p.op == "JZ" || p.op == "JNZ") && !p.a.empty() && p.b.empty()) { const auto target = valueOrLabel(p.a); if (!target) { error = "line " + std::to_string(p.line) + ": unknown jump target"; return false; } i.op = p.op == "JMP" ? Op::Jmp : p.op == "JZ" ? Op::Jz : Op::Jnz; i.value = *target; }
        else if (r && !p.b.empty()) { const auto value = number(p.b); if (!value) { error = "line " + std::to_string(p.line) + ": expected a 16-bit number"; return false; } i.reg = *r; i.value = *value; if (p.op == "MOV") i.op = Op::MovImm; else if (p.op == "ADD") i.op = Op::AddImm; else if (p.op == "SUB") i.op = Op::SubImm; else if (p.op == "IN") i.op = Op::In; else if (p.op == "OUT") i.op = Op::Out; else { error = "line " + std::to_string(p.line) + ": unknown instruction"; return false; } }
        else { error = "line " + std::to_string(p.line) + ": invalid operands"; return false; }
        code.push_back(i);
    }
    return true;
}
void usage() { std::cout << "Usage:\n  asm16.exe <source.asm> [-o program.exe] [--listing]\n  asm16.exe --run <program.exe> [--auto] [--disk <folder>]\n"; }
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    const std::string first = argv[1];
    if (first == "--run") {
        if (argc < 3) { usage(); return 1; }
        bool automatic = false; fs::path disk = "asm16_disk";
        for (int i = 3; i < argc; ++i) { const std::string option = argv[i]; if (option == "--auto") automatic = true; else if (option == "--disk" && i + 1 < argc) disk = argv[++i]; else { std::cerr << "Unknown run option: " << option << '\n'; return 1; } }
        std::vector<Instruction> code; std::string error; if (!readExecutable(argv[2], code, error)) { std::cerr << error << '\n'; return 1; }
        Machine machine(code, disk);
        while (!machine.halted) { if (!automatic) { std::cout << "[IP=" << machine.ip << "] Enter=step, q=stop: "; std::string command; if (!std::getline(std::cin, command) || command == "q") break; } if (!machine.step(std::cout, std::cin, error)) { std::cerr << error << '\n'; return 1; } }
        return 0;
    }
    fs::path source = first, output = source; output.replace_extension(".exe"); bool listing = false;
    for (int i = 2; i < argc; ++i) { const std::string option = argv[i]; if (option == "-o" && i + 1 < argc) output = argv[++i]; else if (option == "--listing") listing = true; else { std::cerr << "Unknown compiler option: " << option << '\n'; return 1; } }
    std::vector<Instruction> code; std::string error; if (!assemble(source, code, error) || !writeExecutable(output, code, error)) { std::cerr << error << '\n'; return 1; }
    std::cout << "Compiled " << source << " -> " << output << " (" << code.size() << " instructions)\n";
    if (listing) for (size_t i = 0; i < code.size(); ++i) std::cout << std::setw(4) << i << "  op=" << static_cast<int>(code[i].op) << " reg=" << static_cast<int>(code[i].reg) << " value=0x" << std::hex << code[i].value << std::dec << '\n';
}
