#include <stdlib.h>

int main(int argc, char **argv) {
  asm inline(".insn 0x2E\n"
             ".insn 0x0F\n"
             ".insn 0x84\n"
             ".insn 0x04\n"
             // Bias: T
//             ".insn 0x30\n"
             // Bias: NT
             ".insn 0x00\n"
             ".insn 0x00\n"
             ".insn 0x00\n");

  if (argc == 1) {
    return EXIT_SUCCESS;
  } else {
    return EXIT_FAILURE;
  }
}
