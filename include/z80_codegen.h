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
#ifdef __cplusplus
}
#endif
