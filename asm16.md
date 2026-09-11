# ASM16

`asm16.exe` compiles ASM16 source into a portable ASM16 `.exe` bytecode file.
It never executes source files directly: compile first, then run the resulting
executable.

```sh
make asm16.exe
./asm16.exe hello.asm -o hello.exe --listing
./asm16.exe --run hello.exe                 # one instruction per Enter
./asm16.exe --run hello.exe --auto          # run until HLT
```

The compiler accepts the source path and `-o <path>` output parameter. If `-o`
is omitted, the source filename is used with `.exe` extension.

## Instructions

Registers are `AX`, `BX`, `CX`, and `DX`. Numbers accept decimal or `0x` hex.

* `MOV reg, value`, `ADD reg, value`, `SUB reg, value`
* `IN reg, port`, `OUT reg, port`
* `JMP label`, `JZ label`, `JNZ label`, `NOP`, `HLT`

## Ports

| Port | Direction | Purpose |
|---|---|---|
| `0x00` | IN | terminal input, one character from a line |
| `0x01` | OUT | terminal numeric value |
| `0x02` | IN | input character (ASCII) |
| `0x03` | OUT | output character (ASCII) |
| `0x10` | IN | milliseconds since program start (low 16 bits) |
| `0x20` | IN | last input-character scan/ASCII code |
| `0x30` | OUT | disk command byte stream |
| `0xE9` | OUT | debug character output |

For port `0x30`, output each character of a command and finish with a newline.
The disk root defaults to `asm16_disk` and can be changed with `--disk`. The
supported commands are `MKDIR folder/name` and `CREATE folder/name/file.txt`.
Thus a program explicitly controls both folder creation and the new file name.

## Example

```asm
; Print A to the terminal.
MOV AX, 65
OUT AX, 0x03
HLT
```
