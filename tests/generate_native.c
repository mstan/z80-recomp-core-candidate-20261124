#include "z80_codegen.h"
#include <stdlib.h>
int main(int argc,char **argv){
 if(argc!=2)return 1;FILE *f=fopen(argv[1],"w");if(!f)return 1;
 fputs("#include <z80_ops.h>\n",f);
 static const unsigned char ops[][4]={{0x3e,0x7f},{0xc6,1},{0xdd,0x21,0,0x10},{0xdd,0x36,0xff,0x42},{0xcd,0x34,0x12},{0xc9},{0xcb,0x7f},{0xed,0xb0}};
 static const unsigned len[]={2,2,4,4,3,1,2,2};
 for(unsigned i=0;i<8;++i){Z80Insn in;if(z80_decode(ops[i],len[i],0x100,&in)!=len[i])return 2;
  fprintf(f,"void native_%u(Z80State *s){\n",i);
  if(z80_emit_step_body(f,&in,0x100)!=Z80_EMIT_OK)return 3;fputs("}\n",f);}
 const unsigned char jump[]={0xc3,0xb2,0};Z80Insn in;
 if(z80_decode(jump,3,0x100,&in)!=3)return 4;
 fputs("void native_live_jump(Z80State *s){\n",f);
 if(z80_emit_live_operand_case(f,&in,0x100)!=Z80_EMIT_OK)return 5;
 fputs("}\n",f);
 return fclose(f)!=0;
}
