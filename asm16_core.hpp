#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// The executable format deliberately has no host executable code in it.  It is
// bytecode run by asm16.exe, which makes compiled programs portable and safe.
namespace asm16 {
namespace fs = std::filesystem;

constexpr std::array<char, 8> kMagic{{'A', 'S', 'M', '1', '6', 'E', 'X', 'E'}};

enum class Op : uint8_t { Nop, MovImm, AddImm, SubImm, In, Out, Jmp, Jz, Jnz, Hlt };

struct Instruction {
    Op op = Op::Nop;
    uint8_t reg = 0;
    uint16_t value = 0;
};

inline void write16(std::ostream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

inline bool read16(std::istream& in, uint16_t& value) {
    const int low = in.get(), high = in.get();
    if (low == EOF || high == EOF) return false;
    value = static_cast<uint16_t>(low | (high << 8));
    return true;
}

inline bool writeExecutable(const fs::path& path, const std::vector<Instruction>& code, std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { error = "cannot create " + path.string(); return false; }
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write16(out, static_cast<uint16_t>(code.size()));
    for (const Instruction& instruction : code) {
        out.put(static_cast<char>(instruction.op));
        out.put(static_cast<char>(instruction.reg));
        write16(out, instruction.value);
    }
    if (!out) { error = "cannot write " + path.string(); return false; }
    return true;
}

inline bool readExecutable(const fs::path& path, std::vector<Instruction>& code, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "cannot open " + path.string(); return false; }
    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != kMagic) { error = "not an ASM16 executable: " + path.string(); return false; }
    uint16_t count = 0;
    if (!read16(in, count)) { error = "truncated executable header"; return false; }
    code.clear(); code.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        const int op = in.get(), reg = in.get(); uint16_t value = 0;
        if (op == EOF || reg == EOF || !read16(in, value) || op > static_cast<int>(Op::Hlt)) {
            error = "truncated or invalid executable"; return false;
        }
        code.push_back({static_cast<Op>(op), static_cast<uint8_t>(reg), value});
    }
    return true;
}

class Machine {
public:
    std::array<uint16_t, 4> registers{}; // AX, BX, CX, DX
    uint16_t ip = 0;
    bool zero = false;
    bool halted = false;
    std::string diskBuffer;
    std::string terminalInput;
    size_t terminalInputOffset = 0;
    uint16_t lastKey = 0;

    explicit Machine(const std::vector<Instruction>& instructions, fs::path diskRoot = "asm16_disk")
        : code(instructions), diskRoot(std::move(diskRoot)), started(std::chrono::steady_clock::now()) {
        fs::create_directories(this->diskRoot);
    }

    bool step(std::ostream& terminal, std::istream& input, std::string& error) {
        if (halted) return true;
        if (ip >= code.size()) { halted = true; error = "instruction pointer is outside the program"; return false; }
        const Instruction instruction = code[ip++];
        if (instruction.reg >= registers.size() && instruction.op != Op::Jmp && instruction.op != Op::Jz && instruction.op != Op::Jnz && instruction.op != Op::Hlt) {
            error = "invalid register"; halted = true; return false;
        }
        switch (instruction.op) {
            case Op::Nop: break;
            case Op::MovImm: registers[instruction.reg] = instruction.value; break;
            case Op::AddImm: registers[instruction.reg] += instruction.value; zero = registers[instruction.reg] == 0; break;
            case Op::SubImm: registers[instruction.reg] -= instruction.value; zero = registers[instruction.reg] == 0; break;
            case Op::In: registers[instruction.reg] = readPort(instruction.value, input); zero = registers[instruction.reg] == 0; break;
            case Op::Out: writePort(instruction.value, registers[instruction.reg], terminal, error); break;
            case Op::Jmp: ip = instruction.value; break;
            case Op::Jz: if (zero) ip = instruction.value; break;
            case Op::Jnz: if (!zero) ip = instruction.value; break;
            case Op::Hlt: halted = true; break;
        }
        return error.empty();
    }

private:
    std::vector<Instruction> code;
    fs::path diskRoot;
    std::chrono::steady_clock::time_point started;

    uint16_t readPort(uint16_t port, std::istream& input) {
        if (port == 0x00) {
            if (terminalInputOffset >= terminalInput.size()) {
                std::getline(input, terminalInput);
                terminalInput.push_back('\n'); terminalInputOffset = 0;
            }
            return static_cast<uint8_t>(terminalInput[terminalInputOffset++]);
        }
        if (port == 0x02) {
            const int character = input.get();
            lastKey = character == EOF ? 0 : static_cast<uint8_t>(character);
            return lastKey;
        }
        if (port == 0x10) return static_cast<uint16_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        if (port == 0x20) return lastKey;
        return 0;
    }

    void writePort(uint16_t port, uint16_t value, std::ostream& terminal, std::string& error) {
        if (port == 0x01) terminal << value << '\n';
        else if (port == 0x03 || port == 0xE9) terminal << static_cast<char>(value & 0xff) << std::flush;
        else if (port == 0x30) {
            const char character = static_cast<char>(value & 0xff);
            if (character == '\n') { executeDiskCommand(error); diskBuffer.clear(); }
            else diskBuffer.push_back(character);
        }
    }

    void executeDiskCommand(std::string& error) {
        const size_t space = diskBuffer.find(' ');
        const std::string command = diskBuffer.substr(0, space);
        const std::string argument = space == std::string::npos ? "" : diskBuffer.substr(space + 1);
        fs::path path = (diskRoot / argument).lexically_normal();
        if (argument.empty() || path.string().find("..") != std::string::npos) { error = "disk: invalid path"; return; }
        std::error_code ec;
        if (command == "MKDIR") fs::create_directories(path, ec);
        else if (command == "CREATE") { fs::create_directories(path.parent_path(), ec); std::ofstream file(path); if (!file) ec = std::make_error_code(std::errc::io_error); }
        else { error = "disk: use MKDIR <folder> or CREATE <filename>"; return; }
        if (ec) error = "disk: " + ec.message();
    }
};
} // namespace asm16
