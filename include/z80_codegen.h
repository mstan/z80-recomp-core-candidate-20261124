#pragma once
#include "z80_decoder.h"
#include <stdio.h>
typedef enum {Z80_EMIT_OK,Z80_EMIT_INVALID,Z80_EMIT_UNSUPPORTED,Z80_EMIT_IO} Z80EmitResult;
/* Emits one native operation body using Z80State *s and the existing sms_* bus ABI.
   Caller guards instruction identity, owns scheduling, and creates function boundaries.
   No process exit on unsupported instructions; unsupported output is transactional.
   Calls are independent across threads; recursive use on one thread is unsupported. */
#ifdef __cplusplus
extern "C" {
#endif
Z80EmitResult z80_emit_step_body(FILE *,const Z80Insn *,uint16_t address);
/* Reuse one caller-owned seekable update stream for a complete image. Scratch
 * differs from output; neither is closed. Old scratch tail bytes are excluded.
 * This avoids exhausting host temporary-name pools for large generations. */
Z80EmitResult z80_emit_step_body_with_scratch(FILE *output,FILE *scratch,const Z80Insn *,uint16_t address);
/* Emit a guarded native case for supported mutable operand templates.
 * Opcode shape stays statically decoded; only operands are read live.
 * Unsupported shapes leave output untouched and use the ordinary guarded case. */
Z80EmitResult z80_emit_live_operand_case(FILE *,const Z80Insn *,uint16_t address);
Z80EmitResult z80_emit_live_operand_case_with_scratch(FILE *output,FILE *scratch,const Z80Insn *,uint16_t address);
#ifdef __cplusplus
}
#endif
