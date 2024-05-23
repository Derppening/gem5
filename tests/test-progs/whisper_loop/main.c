#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  asm inline(".insn 0x2E\n"
             ".insn 0x0F\n"
             ".insn 0x84\n"
             ".insn 0x20\n"
             ".insn 0x10\n"
             ".insn 0x00\n"
             ".insn 0x00\n");

  for (int i = 0; i < 8; ++i) {
    printf("Hello World!\n");
  }
}
