# mJS3 - a JS engine for embedded Arduino IDE systems

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

mJS3 is a single-source-file JavaScript engine for Arduino IDE microcontrollers.

## Demo Install

- Download (Clone) this github repository and copy the folder (as is no need to rename it) into your user Arduino Folder (C:\Users\USER\Documents\Arduino\mjs3-Arduino)
- Load the sketch using the Arduino IDE menu (File->Sketchbook->mjs3-Arduino)
- Build and Upload (*note: currently does not build on AVR platforms)

## Features

- Clean ISO C, ISO C++. Builds on old (VC98) and modern compilers, from 8-bit (e.g. Arduino mini) to 64-bit systems
- No dependencies
- Implements a restricted subset of ES6 with limitations
- Preallocates all necessary memory and never calls `malloc`, `realloc`
  at run time. Upon OOM, the VM is halted
- Object pool, property pool, and string pool sizes are defined at compile time
- The minimal configuration takes only a few hundred bytes of RAM
- RAM usage: an object takes 6 bytes, each property: 16 bytes,
  a string: length + 6 bytes, any other type: 4 bytes
- Strings are byte strings, not Unicode.
  For example, `'ы'.length === 2`, `'ы'[0] === '\xd1'`, `'ы'[1] === '\x8b'`
- Limitations: max string length is 256 bytes, numbers hold
  32-bit float value, no standard JS library
- mJS VM executes JS source directly, no AST/bytecode is generated
- Simple FFI API to inject existing C functions into JS

## Example - blinky in JavaScript on Arduino Platform

```c++
#define MJS_STRING_POOL_SIZE 200      // Buffer for all strings
#include "mjs3-Arduino.c"                     

extern void myDelay(int x) { 
  delay(x);
}

extern void myDigitalWrite(int x, int y) {
  digitalWrite(x, y);
}

void setup() {
  pinMode(16, OUTPUT);                                  // Initialize the LED_BUILTIN pin as an output
  
  struct mjs *vm = mjs_create();                        // Create JS instance
  mjs_ffi(vm, "delay", (cfn_t) myDelay, "vi");          // Import delay()
  mjs_ffi(vm, "write", (cfn_t) myDigitalWrite, "vii");  // Import write()
  
  mjs_eval(vm, "while (1) { write(16, 0); delay(500); write(16, 1); delay(500); }", -1);
}

void loop() {
  //delay(1000);
}

```

```
Sketch uses 272668 bytes (26%) of program storage space. Maximum is 1044464 bytes.
Global variables use 27812 bytes (33%) of dynamic memory, leaving 54108 bytes for local variables. Maximum is 81920 bytes.
```

## Supported standard operations and constructs

| Name              |  Operation                   |
| ----------------- | ---------------------------- |
| Operations        | All but `!=`, `==`. Use `!==`, `===` instead |
| typeof            | `typeof(...)`                |
| delete            | `delete obj.k`               |
| while  					  | `while (...) {...}`          |
| Declarations      | `let a, b, c = 12.3, d = 'a'; ` |
| Simple types      | `let a = null, b = undefined, c = false, d = true;` |
| Functions         | `let f = function(x, y) { return x + y; }; ` |
| Objects           | `let obj = {a: 1, f: function(x) { return x * 2}}; obj.f();` |
| Arrays            | `let arr = [1, 2, 'hi there']` |


## Unsupported standard operations and constructs

| Name              |  Operation                                |
| ----------------- | ----------------------------------------- |
| Loops/switch      | `for (...) { ... }`,`for (let k in obj) { ... }`, `do { ... } while (...)`, `switch (...) {...}` |
| Equality          | `==`, `!=`  (note: use strict equality `===`, `!==`) |
| var               | `var ...`  (note: use `let ...`) |
| Closures          | `let f = function() { let x = 1; return function() { return x; } };`  |
| Const, etc        | `const ...`, `await ...` , `void ...` , `new ...`, `instanceof ...`  |
| Standard types    | No `Date`, `ReGexp`, `Function`, `String`, `Number` |
| Prototypes        | No prototype based inheritance |

## Supported non-standard JS API

| Function          |  Description                              |
| ----------------- | ----------------------------------------- |
| `s[offset]`       | Return byte value at `offset`. `s` is either a string, or a number. A number is interprepted as `uint8_t *` pointer. Example: `'abc'[0]` returns 0x61. To read a byte at address `0x100`, use `0x100[0];`. | |


## LICENSE

Dual license: GPLv2 or commercial. For commercial
licensing, please contact support@mongoose-os.com
