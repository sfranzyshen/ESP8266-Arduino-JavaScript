#include <mjs3.h>                    

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
