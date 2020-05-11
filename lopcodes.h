/*
** $Id: lopcodes.h,v 1.149.1.1 2017/04/19 17:20:42 roberto Exp $
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include "llimits.h"


/*===========================================================================
  We assume that instructions are unsigned numbers.
  All instructions have an opcode in the first 6 bits.
  Instructions can have the following fields:
	'A' : 8 bits
	'B' : 9 bits
	'C' : 9 bits
	'Ax' : 26 bits ('A', 'B', and 'C' together)
	'Bx' : 18 bits ('B' and 'C' together)
	'sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.

  1)每条指令都会对一个对象做出影响，受影响的对象被称为 A。它由 8 bits 来表示。 A 通常是一个寄存器的索引，也可能是对 Upvalue 的操作。
2)作用到 A 的参数一般有两个，每个参数 由 9 bits 表示，分别称为 B 和 C。
3)一部分指令不需要两个操作参数，这时候可以把 B 和 C 合并为一个 18 bits 的整数 Bx 来适应更大的范围。
4)当操作码涉及到跳转指令时，这个参数表示跳转偏移量。向前跳转需要设置偏移量为一个负数。这类指令需要带符号信息来区别，记作 sBx。 其中0被表示为 2^17 ;  1 则表示为 2^17 + 1  ;  -1 表示为 2^17 - 1 。
5)Lua VM 在运行期，会将需要的常量加载到 寄存器中（Lua 栈），然后利用这些寄存器做相应的工作。 加载常量的操作码 为LOADK，它由两个参数 A ,Bx ,这个操作把Bx 所指的常量加载到 A 所指的寄存器中。 Bx 有 18 bit 长，所以 LOADK 这个操作只能索引到 2^18 个常量。 为了扩大索引常量的上限，提供了LOADKX，它将常量索引号放在了接下来的一条EXTRAARG 指令中。 OP_EXTRAARG 指令 把 opcode所占的 8bit 以外的26 bit 都用于参数表示， 称之为* Ax*。 

参数 A、B、C 一般用来存放指令操作数据的地址（索引），而地址（索引）有以下三种： 
1. 寄存器 idx 
2. 常量表 idx 
3. upvalue idx 
===========================================================================*/


enum OpMode {iABC, iABx, iAsBx, iAx};  /* basic instruction format */


/*
** size and position of opcode arguments.
*/
#define SIZE_C		9
#define SIZE_B		9
#define SIZE_Bx		(SIZE_C + SIZE_B)
#define SIZE_A		8
#define SIZE_Ax		(SIZE_C + SIZE_B + SIZE_A)

#define SIZE_OP		6

#define POS_OP		0
#define POS_A		(POS_OP + SIZE_OP)
#define POS_C		(POS_A + SIZE_A)
#define POS_B		(POS_C + SIZE_C)
#define POS_Bx		POS_C
#define POS_Ax		POS_A


/*
** limits for opcode arguments.
** we use (signed) int to manipulate most arguments,
** so they must fit in LUAI_BITSINT-1 bits (-1 for sign)
*/
#if SIZE_Bx < LUAI_BITSINT-1
#define MAXARG_Bx        ((1<<SIZE_Bx)-1)
#define MAXARG_sBx        (MAXARG_Bx>>1)         /* 'sBx' is signed */
#else
#define MAXARG_Bx        MAX_INT
#define MAXARG_sBx        MAX_INT
#endif

#if SIZE_Ax < LUAI_BITSINT-1
#define MAXARG_Ax	((1<<SIZE_Ax)-1)
#else
#define MAXARG_Ax	MAX_INT
#endif


#define MAXARG_A        ((1<<SIZE_A)-1)
#define MAXARG_B        ((1<<SIZE_B)-1)
#define MAXARG_C        ((1<<SIZE_C)-1)


/* creates a mask with 'n' 1 bits at position 'p' */
#define MASK1(n,p)	((~((~(Instruction)0)<<(n)))<<(p))

/* creates a mask with 'n' 0 bits at position 'p' */
#define MASK0(n,p)	(~MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/

#define GET_OPCODE(i)	(cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0)))
#define SET_OPCODE(i,o)	((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
		((cast(Instruction, o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))

#define getarg(i,pos,size)	(cast(int, ((i)>>pos) & MASK1(size,0)))
#define setarg(i,v,pos,size)	((i) = (((i)&MASK0(size,pos)) | \
                ((cast(Instruction, v)<<pos)&MASK1(size,pos))))

#define GETARG_A(i)	getarg(i, POS_A, SIZE_A)
#define SETARG_A(i,v)	setarg(i, v, POS_A, SIZE_A)

#define GETARG_B(i)	getarg(i, POS_B, SIZE_B)
#define SETARG_B(i,v)	setarg(i, v, POS_B, SIZE_B)

#define GETARG_C(i)	getarg(i, POS_C, SIZE_C)
#define SETARG_C(i,v)	setarg(i, v, POS_C, SIZE_C)

#define GETARG_Bx(i)	getarg(i, POS_Bx, SIZE_Bx)
#define SETARG_Bx(i,v)	setarg(i, v, POS_Bx, SIZE_Bx)

#define GETARG_Ax(i)	getarg(i, POS_Ax, SIZE_Ax)
#define SETARG_Ax(i,v)	setarg(i, v, POS_Ax, SIZE_Ax)

#define GETARG_sBx(i)	(GETARG_Bx(i)-MAXARG_sBx)
#define SETARG_sBx(i,b)	SETARG_Bx((i),cast(unsigned int, (b)+MAXARG_sBx))


#define CREATE_ABC(o,a,b,c)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, b)<<POS_B) \
			| (cast(Instruction, c)<<POS_C))

#define CREATE_ABx(o,a,bc)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, bc)<<POS_Bx))

#define CREATE_Ax(o,a)		((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_Ax))


/*
** Macros to operate RK indices
*/

/* this bit 1 means constant (0 means register) */
#define BITRK		(1 << (SIZE_B - 1))

/* test whether value is a constant */
/**
 * 判断这个数据的第八位是不是l ，如果是，则认为 应该从K数组中获取数据，否则就是从函数战寄存器中获取数据。 
 */
#define ISK(x)		((x) & BITRK)

/* gets the index of the constant */
#define INDEXK(r)	((int)(r) & ~BITRK)

#if !defined(MAXINDEXRK)  /* (for debugging only) */
#define MAXINDEXRK	(BITRK - 1)
#endif

/* code a constant index as a RK value */
#define RKASK(x)	((x) | BITRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG		MAXARG_A


/**
 * R(x) - register 表示这一定是 寄存器索引（一定要操作Lua 栈） 
 * Kst(x) - constant (in constant table) 
 * RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x) 表示这有可能是 一个寄存器索引 或 是一个常量索引，RK 只能用 参数B 与 参数 C (SIZE_B = SIZE_C = 9)，其中参数的最高位区分 寄存器索引与常量索引。
 * pc 程序计数器（ program counter ），这个数据用于指示当前指令的地址 
 * Upvalue(n) upvalue数组中的第n个数据
 * Gbl[sym] 全局符号表中取名为sym的数据 
 * sBx 有符号整数 用于表示跳转偏移量
*/


/*
** grep "ORDER OP" if you change these enums
*/

typedef enum {
/*----------------------------------------------------------------------
name		args	description
------------------------------------------------------------------------*/
OP_MOVE,/*	A B	R(A) := R(B)					从R(B）中取数据赋值给R(A) */
OP_LOADK,/*	A Bx	R(A) := Kst(Bx)					从Kst(Bx）常盘数组中取数据赋值给R(A) */
OP_LOADKX,/*	A 	R(A) := Kst(extra arg)				*/
OP_LOADBOOL,/*	A B C	R(A) := (Bool)B; if (C) pc++			取B参数的布尔值赋值给R(A），如果满足C为真的条件，则将pc指 针递增，即执行下一条指令 */
OP_LOADNIL,/*	A B	R(A), R(A+1), ..., R(A+B) := nil		从寄存器R(A）到R(B）的数据赋值为nil*/
OP_GETUPVAL,/*	A B	R(A) := UpValue[B]				从UpValue数组中取值赋值给R(A) */

OP_GETTABUP,/*	A B C	R(A) := UpValue[B][RK(C)]			*/
OP_GETTABLE,/*	A B C	R(A) := R(B)[RK(C)]				以RK(C）作为表索引，以R(B）的数据作为表，取出来的数据赋值 给R(A) */

OP_SETTABUP,/*	A B C	UpValue[A][RK(B)] := RK(C)			*/
OP_SETUPVAL,/*	A B	UpValue[B] := R(A)				将R(A）的值赋值给以B作为upvalue数组索引的变量*/
OP_SETTABLE,/*	A B C	R(A)[RK(B)] := RK(C)				将RK(C）的值赋值给R(A）表中索引为RK(B）的变量*/

OP_NEWTABLE,/*	A B C	R(A) := {} (size = B,C)				创建一个新的表，并将其赋值给R(A），其中数组部分的初始大小 是B，散列部分的大小是C */

OP_SELF,/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		做好调用成员函数之前的准备， 其中待调用模块赋值到R(A+l) 中，而待调用的成员函数存放在R(A）中，待调用的模块存放在 R(B）中，待调用的函数名存放在RK(C）中
*/

OP_ADD,/*	A B C	R(A) := RK(B) + RK(C)				加法操作 */
OP_SUB,/*	A B C	R(A) := RK(B) - RK(C)				减法操作 */
OP_MUL,/*	A B C	R(A) := RK(B) * RK(C)				乘法操作 */
OP_MOD,/*	A B C	R(A) := RK(B) % RK(C)				模操作 */
OP_POW,/*	A B C	R(A) := RK(B) ^ RK(C)				乘方操作 */
OP_DIV,/*	A B C	R(A) := RK(B) / RK(C)				除法操作 */
OP_IDIV,/*	A B C	R(A) := RK(B) // RK(C)				*/
OP_BAND,/*	A B C	R(A) := RK(B) & RK(C)				*/
OP_BOR,/*	A B C	R(A) := RK(B) | RK(C)				*/
OP_BXOR,/*	A B C	R(A) := RK(B) ~ RK(C)				*/
OP_SHL,/*	A B C	R(A) := RK(B) << RK(C)				*/
OP_SHR,/*	A B C	R(A) := RK(B) >> RK(C)				*/
OP_UNM,/*	A B	R(A) := -R(B)					取负操作 */
OP_BNOT,/*	A B	R(A) := ~R(B)					*/
OP_NOT,/*	A B	R(A) := not R(B)				非操作*/
OP_LEN,/*	A B	R(A) := length of R(B)				取长度操作 */

OP_CONCAT,/*	A B C	R(A) := R(B).. ... ..R(C)			连接操作 */

OP_JMP,/*	A sBx	pc+=sBx; if (A) close all upvalues >= R(A - 1)	跳转操作 */
OP_EQ,/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		比较相等操作，如果比较RK(B）和RK(C）所得的结果不等于A，那么 递增pc指令 */
OP_LT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++		比较小于操作，如果比较RK(B）小子RK(C）所得的结果不等于A，那 么递增pc指令 */
OP_LE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++		比较小于等于操作，如果比较RK(B）小于等于RK(C）所得的结果不 等于A，那么递增pc指令 */

OP_TEST,/*	A C	if not (R(A) <=> C) then pc++			测试操作，如果R(A）参数的布尔值不等于C，将pc指针加一，直接跳过下一条指令的执行 */
OP_TESTSET,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	测试设置操作，与OP_TEST指令类似，所不同的是当比较的参数 不相等时，执行一个赋值操*/

OP_CALL,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) 调用函数指令，其中函数地址存放在R（时，函数参数数量存放在 B中，有两种情况： I ）为0表示参数从A+l的位置一直到函数梭的 top 位置，这表示函数参数中有另外的函数调用，因为在调用时 并不知道有多少参数，所以只好告诉虚拟机函数参数一直到函 数梭的top位置了； 2 ）大于0时函数参数数量为自－1 */
OP_TAILCALL,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		尾调用操作， R(A）存放函数地址，参数B表示函数参数数盐，意 义与前面OP CALL指令的B参数一样， C参数在这里恒为0表示有多个返回值*/
OP_RETURN,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	返回操作， R(A）表示函数参数的起始地址， B参数用于表示函数 参数数量，有两种情况： I ）为0表示参数从A+l的位置一直到函 数梭的top位置，这表示函数参数中有另外的函数调用，因为在 调用时并不知道有多少参数，所以只好告诉虚拟机函数参数一 直到函数梭的top{i'L置了， 2 ）大子。时函数参数数量为B-1。 参数 C表示函数返回值数量，也有两种情况： I ）为0时表示有可变数 量的值返回； 2 ）为l时表示返回值数量为C-1 */

OP_FORLOOP,/*	A sBx	R(A)+=R(A+2);
			if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) } 数字for的循环操作，根据循环步长来更新循环变茧，判断循环 条件是存终止，如果没有，就跳转到循环体继续执行下一次循 环，否则退出循环。 R(A）存放循环变量的初始值， R(A+l）存放循 环终止值， R(A+2）存放循环步长值， R(A+3）存放循环变盐， sBx参 数存放循环体开始指令的偏移盘 */
OP_FORPREP,/*	A sBx	R(A)-=R(A+2); pc+=sBx				数字for循环准备操作。 R(A）存放循环变量的初始值， R(A+l）存放 循环终止值， R(A+2）存放循环步长值， R(A+3）存放循环变盐， sBx 参数存放紧跟着的OP FORLOOP指令的偏移量*/

OP_TFORCALL,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));	*/
OP_TFORLOOP,/*	A sBx	if R(A+1) ~= nil then { R(A)=R(A+1); pc += sBx } 泛型循环操作*/

OP_SETLIST,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	对表的数组部分进行赋值 */

OP_CLOSURE,/*	A Bx	R(A) := closure(KPROTO[Bx])			创建一个函数对象，其中民数Proto信息存放在Bx巾， 生成的函 R(A), . .. ,R(A+n)) 数对象存放在 R(A）中，这个指令后面可能会跟着f'OVE或者 GET UPVAL指令，取决于引用到的外部参数的位置，这些外部参 数的数量由n决定 */

OP_VARARG,/*	A B	R(A), R(A+1), ..., R(A+B-2) = vararg		可变参数赋值操作 */

OP_EXTRAARG/*	Ax	extra (larger) argument for previous opcode	*/
} OpCode;


#define NUM_OPCODES	(cast(int, OP_EXTRAARG) + 1)



/*===========================================================================
  Notes:
  (*) In OP_CALL, if (B == 0) then B = top. If (C == 0), then 'top' is
  set to last_result+1, so next open instruction (OP_CALL, OP_RETURN,
  OP_SETLIST) may use 'top'.

  (*) In OP_VARARG, if (B == 0) then use actual number of varargs and
  set top (like in OP_CALL with C == 0).

  (*) In OP_RETURN, if (B == 0) then return up to 'top'.

  (*) In OP_SETLIST, if (B == 0) then B = 'top'; if (C == 0) then next
  'instruction' is EXTRAARG(real C).

  (*) In OP_LOADKX, the next 'instruction' is always EXTRAARG.

  (*) For comparisons, A specifies what condition the test should accept
  (true or false).

  (*) All 'skips' (pc++) assume that next instruction is a jump.

===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-1: op mode
** bits 2-3: C arg mode
** bits 4-5: B arg mode
** bit 6: instruction set register A
** bit 7: operator is a test (next instruction must be a jump)
*/

enum OpArgMask {
  OpArgN,  /* argument is not used */
  OpArgU,  /* argument is used */
  OpArgR,  /* argument is a register or a jump offset */
  OpArgK   /* argument is a constant or register/constant */
};

LUAI_DDEC const lu_byte luaP_opmodes[NUM_OPCODES];

#define getOpMode(m)	(cast(enum OpMode, luaP_opmodes[m] & 3))
#define getBMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 4) & 3))
#define getCMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 2) & 3))
#define testAMode(m)	(luaP_opmodes[m] & (1 << 6))
#define testTMode(m)	(luaP_opmodes[m] & (1 << 7))


LUAI_DDEC const char *const luaP_opnames[NUM_OPCODES+1];  /* opcode names */


/* number of list items to accumulate before a SETLIST instruction */
#define LFIELDS_PER_FLUSH	50


#endif
