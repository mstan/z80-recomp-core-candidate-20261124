#include "z80_codegen.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x))abort();}while(0)
int main(void){
 unsigned char op[]={0xdd,0xcb,0xfe,0x46};Z80Insn x;
 CHECK(z80_decode(op,4,0x100,&x)==4 && x.prefix==Z80_PFX_DDCB && x.disp==-2);
 unsigned char branch[]={0x18,0xfc};CHECK(z80_decode(branch,2,0,&x)==2 && x.target==0xfffe);
 CHECK(z80_decode(branch,0,0,&x)==0);
 FILE *f=tmpfile();CHECK(f);memset(&x,0,sizeof x);x.length=1;x.illegal=true;
 CHECK(z80_emit_step_body(f,&x,0)==Z80_EMIT_UNSUPPORTED && ftell(f)==0);
 x.length=0;CHECK(z80_emit_step_body(f,&x,0)==Z80_EMIT_INVALID && ftell(f)==0);
 FILE *scratch=tmpfile(),*reference=tmpfile();CHECK(scratch&&reference);
 CHECK(z80_decode(op,4,0x100,&x)==4);
 CHECK(z80_emit_step_body_with_scratch(f,scratch,&x,0x100)==Z80_EMIT_OK);
 const unsigned char simple[]={0x00};CHECK(z80_decode(simple,1,0,&x)==1);
 CHECK(z80_emit_step_body(reference,&x,0)==Z80_EMIT_OK);
 CHECK(fseek(f,0,SEEK_SET)==0);
 CHECK(z80_emit_step_body_with_scratch(f,scratch,&x,0)==Z80_EMIT_OK);
 CHECK(ftell(f)==ftell(reference)); /* The previous longer scratch tail is excluded. */
 long position=ftell(f);x.illegal=true;
 CHECK(z80_emit_step_body_with_scratch(f,scratch,&x,0)==Z80_EMIT_UNSUPPORTED && ftell(f)==position);
 x.illegal=false;
 CHECK(z80_emit_live_operand_case_with_scratch(f,scratch,&x,0)==Z80_EMIT_UNSUPPORTED && ftell(f)==position);
 CHECK(z80_emit_step_body_with_scratch(f,f,&x,0)==Z80_EMIT_INVALID && ftell(f)==position);
 fclose(scratch);fclose(reference);fclose(f);
 for(unsigned p=0;p<256;++p){unsigned char bytes[]={p,0,0,0};CHECK(z80_decode(bytes,4,0,&x)>0 && x.length<=4);}
 return 0;
}
