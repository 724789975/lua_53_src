/*
** $Id: lcode.c,v 2.112.1.1 2017/04/19 17:20:42 roberto Exp $
** Code generator for Lua
** See Copyright Notice in lua.h
*/

#define lcode_c
#define LUA_CORE

#include "lprefix.h"

#include <math.h>
#include <stdlib.h>

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"

/* Maximum number of registers in a Lua function (must fit in 8 bits) */
#define MAXREGS 255

#define hasjumps(e) ((e)->t != (e)->f)

/*
** If expression is a numeric constant, fills 'v' with its value
** and returns 1. Otherwise, returns 0.
*/
static int tonumeral(const expdesc *e, TValue *v)
{
	if (hasjumps(e))
		return 0; /* not a numeral */
	switch (e->k)
	{
	case VKINT:
		if (v)
			setivalue(v, e->u.ival);
		return 1;
	case VKFLT:
		if (v)
			setfltvalue(v, e->u.nval);
		return 1;
	default:
		return 0;
	}
}

/*
** Create a OP_LOADNIL instruction, but try to optimize: if the previous
** instruction is also OP_LOADNIL and ranges are compatible, adjust
** range of previous instruction instead of emitting a new one. (For
** instance, 'local a; local b' will generate a single opcode.)
*/
void luaK_nil(FuncState *fs, int from, int n)
{
	Instruction *previous;
	int l = from + n - 1; /* last register to set nil */
	if (fs->pc > fs->lasttarget)
	{ /* no jumps to current position? */
		previous = &fs->f->code[fs->pc - 1];
		if (GET_OPCODE(*previous) == OP_LOADNIL)
		{									 /* previous is LOADNIL? */
			int pfrom = GETARG_A(*previous); /* get previous range */
			int pl = pfrom + GETARG_B(*previous);
			if ((pfrom <= from && from <= pl + 1) ||
				(from <= pfrom && pfrom <= l + 1))
			{ /* can connect both? */
				if (pfrom < from)
					from = pfrom; /* from = min(from, pfrom) */
				if (pl > l)
					l = pl; /* l = max(l, pl) */
				SETARG_A(*previous, from);
				SETARG_B(*previous, l - from);
				return;
			}
		} /* else go through */
	}
	luaK_codeABC(fs, OP_LOADNIL, from, n - 1, 0); /* else no optimization */
}

/*
** Gets the destination address of a jump instruction. Used to traverse
** a list of jumps.
*/
static int getjump(FuncState *fs, int pc)
{
	int offset = GETARG_sBx(fs->f->code[pc]);
	if (offset == NO_JUMP) /* point to itself represents end of list */
		return NO_JUMP;	   /* end of list */
	else
		return (pc + 1) + offset; /* turn offset into absolute position */
}

/*
** Fix jump instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua)
*/
static void fixjump(FuncState *fs, int pc, int dest)
{
	Instruction *jmp = &fs->f->code[pc];
	int offset = dest - (pc + 1);
	lua_assert(dest != NO_JUMP);
	if (abs(offset) > MAXARG_sBx)
		luaX_syntaxerror(fs->ls, "control structure too long");
	SETARG_sBx(*jmp, offset);
}

/**
 * Concatenate jump-list 'l2' into jump-list 'l1'
 * 参数11是空悬链表的第一个指令位置,l2是待加入该链表的指令位置
*/
void luaK_concat(FuncState *fs, int *l1, int l2)
{
	/**
   * 如果l2是NO_JUMP,则直接返回,因为这个位置存储的指令不是一个跳转指令
   */
	if (l2 == NO_JUMP)
		return; /* nothing to concatenate? */
	/**
   * 如果l1是NO_JUMP,说明这个跳转链表为空,当前没有空悬的跳转指令在该链表中,直接赋值为l2
   */
	else if (*l1 == NO_JUMP) /* no original list? */
	{
		*l1 = l2; /* 'l1' points to 'l2' */
	}
	/**
   * 11现在是一个非空的跳转链表,首先遍历这个链表到最后一个元素,
   * 其判定标准是跳转位置为NO_JUMP时表示是跳转链表的最后一个元素,然后调用fixjump函数将最后一个元素的跳转位置设置为l2,
   * 这样l2就添加到了该跳转链表中
   */
	else
	{
		int list = *l1;
		int next;
		while ((next = getjump(fs, list)) != NO_JUMP) /* find last element */
			list = next;
		fixjump(fs, list, l2); /* last element links to 'l2' */
	}
}

/**
 * Create a jump instruction and return its position, so its destination
 * can be fixed later (with 'fixjump'). If there are jumps to
 * this position (kept in 'jpc'), link them all together so that
 * 'patchlistaux' will fix all them directly to the final destination.
 * 生成JMP指令时调用的函数
*/
int luaK_jump(FuncState *fs)
{
	int jpc = fs->jpc; /* 预存了当前的 jpc  save list of jumps to here */
	int j;
	fs->jpc = NO_JUMP;						   /* 将当前 FuncState的jpc指针置为 NO_JUMP  no more jumps to here */
	j = luaK_codeAsBx(fs, OP_JMP, 0, NO_JUMP); //调用luaK_codeAsBx生成OP_JMP指令
	luaK_concat(fs, &j, jpc);				   /* 将前面预存的jpc指针加入到新生成的OP_JMP指令的跳转位置中 keep them on hold */
	return j;
}

/*
** Code a 'return' instruction
*/
void luaK_ret(FuncState *fs, int first, int nret)
{
	luaK_codeABC(fs, OP_RETURN, first, nret + 1, 0);
}

/*
** Code a "conditional jump", that is, a test or comparison opcode
** followed by a jump. Return jump position.
*/
static int condjump(FuncState *fs, OpCode op, int A, int B, int C)
{
	luaK_codeABC(fs, op, A, B, C);
	return luaK_jump(fs);
}

/**
 * returns current 'pc' and marks it as a jump target (to avoid wrong
 * optimizations with consecutive instructions not in the same basic block).
 * 跳转到当前指令
*/
int luaK_getlabel(FuncState *fs)
{
	fs->lasttarget = fs->pc;
	return fs->pc;
}

/*
** Returns the position of the instruction "controlling" a given
** jump (that is, its condition), or the jump itself if it is
** unconditional.
*/
static Instruction *getjumpcontrol(FuncState *fs, int pc)
{
	Instruction *pi = &fs->f->code[pc];
	if (pc >= 1 && testTMode(GET_OPCODE(*(pi - 1))))
		return pi - 1;
	else
		return pi;
}

/**
 * Patch destination register for a TESTSET instruction.
 * If instruction in position 'node' is not a TESTSET, return 0 ("fails").
 * Otherwise, if 'reg' is not 'NO_REG', set it as the destination
 * register. Otherwise, change instruction to a simple 'TEST' (produces
 * no register value)
 * 跳转指令不是紧跟在OP_TESTSET指令后面的情况下， patchtestreg返回0，在 patchlistaux 函数中使用 dtarget进行回填操作
 * reg : 需要赋值的目的寄存器地址，也就是OP_TESTSET指令中的参数A，当这个值有效 并且不等于参数B时，直接使用这个值赋值给OP_TESTSET指令的参数A
 * 否则，就是没有寄存器进行赋值，或者寄存器中已经存在值(参数A与参数B相等的情况下)， 此时将原先的OP_TESTSET指令修改为OP_TEST指令
*/
static int patchtestreg(FuncState *fs, int node, int reg)
{
	Instruction *i = getjumpcontrol(fs, node);
	if (GET_OPCODE(*i) != OP_TESTSET)
		return 0; /* cannot patch other instructions */
	if (reg != NO_REG && reg != GETARG_B(*i))
		SETARG_A(*i, reg);
	else
	{
		/* no register to put value or register already has the value;
        change instruction to simple test */
		*i = CREATE_ABC(OP_TEST, GETARG_B(*i), 0, GETARG_C(*i));
	}
	return 1;
}

/*
** Traverse a list of tests ensuring no one produces a value
*/
static void removevalues(FuncState *fs, int list)
{
	for (; list != NO_JUMP; list = getjump(fs, list))
		patchtestreg(fs, list, NO_REG);
}

/**
 * Traverse a list of tests, patching their destination address and
 * registers: tests producing values jump to 'vtarget' (and put their
 * values in 'reg'), other tests jump to 'dtarget'.
 * 遍历一个跳转链表的所有元素，调用于fix_jump函数将跳转地址回填到链表中的每个指令中
 * vtarget :value target
 * dtarget : default target
*/
static void patchlistaux(FuncState *fs, int list, int vtarget, int reg,
						 int dtarget)
{
	while (list != NO_JUMP)
	{
		int next = getjump(fs, list);
		if (patchtestreg(fs, list, reg))
			fixjump(fs, list, vtarget);
		else
			fixjump(fs, list, dtarget); /* jump to default target */
		list = next;
	}
}

/**
 * Ensure all pending jumps to current position are fixed (jumping
 * to current position with no values) and reset list of pending
 * jumps
 * FuncState结构体有一个名为jpc的成员，
 * 它将需要回填为下一个待生成指令地址的跳转指令 链接到一起。
 * 这个操作是在luaK_patchtohere函数中进行的 
*/
static void dischargejpc(FuncState *fs)
{
	patchlistaux(fs, fs->jpc, fs->pc, NO_REG, fs->pc);
	fs->jpc = NO_JUMP;
}

/**
 * Add elements in 'list' to list of pending jumps to "here"
 * (current position)
*/
void luaK_patchtohere(FuncState *fs, int list)
{
	luaK_getlabel(fs); /* mark "here" as a jump target */
	luaK_concat(fs, &fs->jpc, list);
}

/*
** Path all jumps in 'list' to jump to 'target'.
** (The assert means that we cannot fix a jump to a forward address
** because we only know addresses once code is generated.)
*/
void luaK_patchlist(FuncState *fs, int list, int target)
{
	if (target == fs->pc)			/* 'target' is current position? */
		luaK_patchtohere(fs, list); /* add list to pending jumps */
	else
	{
		lua_assert(target < fs->pc);
		patchlistaux(fs, list, target, NO_REG, target);
	}
}

/*
** Path all jumps in 'list' to close upvalues up to given 'level'
** (The assertion checks that jumps either were closing nothing
** or were closing higher levels, from inner blocks.)
*/
void luaK_patchclose(FuncState *fs, int list, int level)
{
	level++; /* argument is +1 to reserve 0 as non-op */
	for (; list != NO_JUMP; list = getjump(fs, list))
	{
		lua_assert(GET_OPCODE(fs->f->code[list]) == OP_JMP &&
				   (GETARG_A(fs->f->code[list]) == 0 ||
					GETARG_A(fs->f->code[list]) >= level));
		SETARG_A(fs->f->code[list], level);
	}
}

/**
 * Emit instruction 'i', checking for array sizes and saving also its
 * line information. Return 'i' position.
 * 每次新生成一个指令最终会调用的函数
 * 将调用函数dischargejpc遍历jpc链表，
 * 使用当前的pc指针进行回填操作
 * 
 * Opcode存放在Proto结构上
 * 其中f->code数组用于存放code
 * fs->pc主要是计数器,标记code的个数及数组下标
**/
static int luaK_code(FuncState *fs, Instruction i)
{
	Proto *f = fs->f;
	dischargejpc(fs); /* 'pc' will change */
	/* put new instruction in code array */
	luaM_growvector(fs->ls->L, f->code, fs->pc, f->sizecode, Instruction,
					MAX_INT, "opcodes");
	f->code[fs->pc] = i;
	/* save corresponding line information */
	luaM_growvector(fs->ls->L, f->lineinfo, fs->pc, f->sizelineinfo, int,
					MAX_INT, "opcodes");
	f->lineinfo[fs->pc] = fs->ls->lastline;
	/**
   * 返回新生成指令的pc指针时，
   * 会将pc指针做一个＋＋操作，
   * 这样下一次再调用luaK_code函数走到dischargejpc函数时，
   * pc指针自然都是指向下一个待生成的指令
   */
	return fs->pc++;
}

/*
** Format and emit an 'iABC' instruction. (Assertions check consistency
** of parameters versus opcode.)
*/
int luaK_codeABC(FuncState *fs, OpCode o, int a, int b, int c)
{
	lua_assert(getOpMode(o) == iABC);
	lua_assert(getBMode(o) != OpArgN || b == 0);
	lua_assert(getCMode(o) != OpArgN || c == 0);
	lua_assert(a <= MAXARG_A && b <= MAXARG_B && c <= MAXARG_C);
	return luaK_code(fs, CREATE_ABC(o, a, b, c));
}

/*
** Format and emit an 'iABx' instruction.
*/
int luaK_codeABx(FuncState *fs, OpCode o, int a, unsigned int bc)
{
	lua_assert(getOpMode(o) == iABx || getOpMode(o) == iAsBx);
	lua_assert(getCMode(o) == OpArgN);
	lua_assert(a <= MAXARG_A && bc <= MAXARG_Bx);
	return luaK_code(fs, CREATE_ABx(o, a, bc));
}

/*
** Emit an "extra argument" instruction (format 'iAx')
*/
static int codeextraarg(FuncState *fs, int a)
{
	lua_assert(a <= MAXARG_Ax);
	return luaK_code(fs, CREATE_Ax(OP_EXTRAARG, a));
}

/*
** Emit a "load constant" instruction, using either 'OP_LOADK'
** (if constant index 'k' fits in 18 bits) or an 'OP_LOADKX'
** instruction with "extra argument".
*/
int luaK_codek(FuncState *fs, int reg, int k)
{
	if (k <= MAXARG_Bx)
		return luaK_codeABx(fs, OP_LOADK, reg, k);
	else
	{
		int p = luaK_codeABx(fs, OP_LOADKX, reg, 0);
		codeextraarg(fs, k);
		return p;
	}
}

/*
** Check register-stack level, keeping track of its maximum size
** in field 'maxstacksize'
*/
void luaK_checkstack(FuncState *fs, int n)
{
	int newstack = fs->freereg + n;
	if (newstack > fs->f->maxstacksize)
	{
		if (newstack >= MAXREGS)
			luaX_syntaxerror(fs->ls,
							 "function or expression needs too many registers");
		fs->f->maxstacksize = cast_byte(newstack);
	}
}

/*
** Reserve 'n' registers in register stack
*/
void luaK_reserveregs(FuncState *fs, int n)
{
	luaK_checkstack(fs, n);
	fs->freereg += n;
}

/*
** Free register 'reg', if it is neither a constant index nor
** a local variable.
)
*/
static void freereg(FuncState *fs, int reg)
{
	if (!ISK(reg) && reg >= fs->nactvar)
	{
		fs->freereg--;
		lua_assert(reg == fs->freereg);
	}
}

/*
** Free register used by expression 'e' (if any)
*/
static void freeexp(FuncState *fs, expdesc *e)
{
	if (e->k == VNONRELOC)
		freereg(fs, e->u.info);
}

/*
** Free registers used by expressions 'e1' and 'e2' (if any) in proper
** order.
*/
static void freeexps(FuncState *fs, expdesc *e1, expdesc *e2)
{
	int r1 = (e1->k == VNONRELOC) ? e1->u.info : -1;
	int r2 = (e2->k == VNONRELOC) ? e2->u.info : -1;
	if (r1 > r2)
	{
		freereg(fs, r1);
		freereg(fs, r2);
	}
	else
	{
		freereg(fs, r2);
		freereg(fs, r1);
	}
}

/*
** Add constant 'v' to prototype's list of constants (field 'k').
** Use scanner's table to cache position of constants in constant list
** and try to reuse constants. Because some values should not be used
** as keys (nil cannot be a key, integer keys can collapse with float
** keys), the caller must provide a useful 'key' for indexing the cache.
*/
static int addk(FuncState *fs, TValue *key, TValue *v)
{
	lua_State *L = fs->ls->L;
	Proto *f = fs->f;
	TValue *idx = luaH_set(L, fs->ls->h, key); /* index scanner table */
	int k, oldsize;
	if (ttisinteger(idx))
	{ /* is there an index there? */
		k = cast_int(ivalue(idx));
		/* correct value? (warning: must distinguish floats from integers!) */
		if (k < fs->nk && ttype(&f->k[k]) == ttype(v) &&
			luaV_rawequalobj(&f->k[k], v))
			return k; /* reuse index */
	}
	/* constant not found; create a new entry */
	oldsize = f->sizek;
	k = fs->nk;
	/* numerical value does not need GC barrier;
     table has no metatable, so it does not need to invalidate cache */
	setivalue(idx, k);
	luaM_growvector(L, f->k, k, f->sizek, TValue, MAXARG_Ax, "constants");
	while (oldsize < f->sizek)
		setnilvalue(&f->k[oldsize++]);
	setobj(L, &f->k[k], v);
	fs->nk++;
	luaC_barrier(L, f, v);
	return k;
}

/*
** Add a string to list of constants and return its index.
*/
int luaK_stringK(FuncState *fs, TString *s)
{
	TValue o;
	setsvalue(fs->ls->L, &o, s);
	return addk(fs, &o, &o); /* use string itself as key */
}

/*
** Add an integer to list of constants and return its index.
** Integers use userdata as keys to avoid collision with floats with
** same value; conversion to 'void*' is used only for hashing, so there
** are no "precision" problems.
*/
int luaK_intK(FuncState *fs, lua_Integer n)
{
	TValue k, o;
	setpvalue(&k, cast(void *, cast(size_t, n)));
	setivalue(&o, n);
	return addk(fs, &k, &o);
}

/*
** Add a float to list of constants and return its index.
*/
static int luaK_numberK(FuncState *fs, lua_Number r)
{
	TValue o;
	setfltvalue(&o, r);
	return addk(fs, &o, &o); /* use number itself as key */
}

/*
** Add a boolean to list of constants and return its index.
*/
static int boolK(FuncState *fs, int b)
{
	TValue o;
	setbvalue(&o, b);
	return addk(fs, &o, &o); /* use boolean itself as key */
}

/*
** Add nil to list of constants and return its index.
*/
static int nilK(FuncState *fs)
{
	TValue k, v;
	setnilvalue(&v);
	/* cannot use nil as key; instead use table itself to represent nil */
	sethvalue(fs->ls->L, &k, fs->ls->h);
	return addk(fs, &k, &v);
}

/*
** Fix an expression to return the number of results 'nresults'.
** Either 'e' is a multi-ret expression (function call or vararg)
** or 'nresults' is LUA_MULTRET (as any expression can satisfy that).
*/
void luaK_setreturns(FuncState *fs, expdesc *e, int nresults)
{
	if (e->k == VCALL)
	{ /* expression is an open function call? */
		SETARG_C(getinstruction(fs, e), nresults + 1);
	}
	else if (e->k == VVARARG)
	{
		Instruction *pc = &getinstruction(fs, e);
		SETARG_B(*pc, nresults + 1);
		SETARG_A(*pc, fs->freereg);
		luaK_reserveregs(fs, 1);
	}
	else
		lua_assert(nresults == LUA_MULTRET);
}

/*
** Fix an expression to return one result.
** If expression is not a multi-ret expression (function call or
** vararg), it already returns one result, so nothing needs to be done.
** Function calls become VNONRELOC expressions (as its result comes
** fixed in the base register of the call), while vararg expressions
** become VRELOCABLE (as OP_VARARG puts its results where it wants).
** (Calls are created returning one result, so that does not need
** to be fixed.)
*/
void luaK_setoneret(FuncState *fs, expdesc *e)
{
	if (e->k == VCALL)
	{ /* expression is an open function call? */
		/* already returns 1 value */
		lua_assert(GETARG_C(getinstruction(fs, e)) == 2);
		e->k = VNONRELOC; /* result has fixed position */
		e->u.info = GETARG_A(getinstruction(fs, e));
	}
	else if (e->k == VVARARG)
	{
		SETARG_B(getinstruction(fs, e), 2);
		e->k = VRELOCABLE; /* can relocate its simple result */
	}
}

/*
** Ensure that expression 'e' is not a variable.
*/
void luaK_dischargevars(FuncState *fs, expdesc *e)
{
	switch (e->k)
	{
	case VLOCAL:
	{					  /* already in a register */
		e->k = VNONRELOC; /* becomes a non-relocatable value */
		break;
	}
	case VUPVAL:
	{ /* move value to some (pending) register */
		e->u.info = luaK_codeABC(fs, OP_GETUPVAL, 0, e->u.info, 0);
		e->k = VRELOCABLE;
		break;
	}
	case VINDEXED:
	{
		OpCode op;
		freereg(fs, e->u.ind.idx);
		if (e->u.ind.vt == VLOCAL)
		{ /* is 't' in a register? */
			freereg(fs, e->u.ind.t);
			op = OP_GETTABLE;
		}
		else
		{
			lua_assert(e->u.ind.vt == VUPVAL);
			op = OP_GETTABUP; /* 't' is in an upvalue */
		}
		e->u.info = luaK_codeABC(fs, op, 0, e->u.ind.t, e->u.ind.idx);
		e->k = VRELOCABLE;
		break;
	}
	case VVARARG:
	case VCALL:
	{
		luaK_setoneret(fs, e);
		break;
	}
	default:
		break; /* there is one value available (somewhere) */
	}
}

/**
 * discharge2reg函数是底层赋值的操作函数。针对值的不同类型进行不同的封装操作码。
 * 布尔类型:则通过luaK_codeABC函数,封装OP_LOADBOOL操作符,参数A为变量名称,参数B为布尔值
 * 对象赋值:如果是两个对象变量之间的赋值,则会封装OP_MOVE操作符,参数A为变量名称,参数B为赋值变量对象地址
 * 全局变量操作:全局变量OP_SETUPVAL操作符,参数A为值,B为变量名称值(这里不太一样)
 * Ensures expression value is in register 'reg' (and therefore
 * 'e' will become a non-relocatable expression).
**/
static void discharge2reg(FuncState *fs, expdesc *e, int reg)
{
	luaK_dischargevars(fs, e);
	switch (e->k)
	{
	case VNIL:
	{
		luaK_nil(fs, reg, 1);
		break;
	}
	case VFALSE:
	case VTRUE:
	{
		luaK_codeABC(fs, OP_LOADBOOL, reg, e->k == VTRUE, 0);
		break;
	}
	case VK:
	{
		luaK_codek(fs, reg, e->u.info);
		break;
	}
	case VKFLT:
	{
		luaK_codek(fs, reg, luaK_numberK(fs, e->u.nval));
		break;
	}
	case VKINT:
	{
		luaK_codek(fs, reg, luaK_intK(fs, e->u.ival));
		break;
	}
	case VRELOCABLE:
	{
		Instruction *pc = &getinstruction(fs, e);
		SETARG_A(*pc, reg); /* instruction will put result in 'reg' */
		break;
	}
	case VNONRELOC:
	{
		if (reg != e->u.info)
			luaK_codeABC(fs, OP_MOVE, reg, e->u.info, 0);
		break;
	}
	default:
	{
		lua_assert(e->k == VJMP);
		return; /* nothing to do... */
	}
	}
	e->u.info = reg;
	e->k = VNONRELOC;
}

/*
** Ensures expression value is in any register.
*/
static void discharge2anyreg(FuncState *fs, expdesc *e)
{
	if (e->k != VNONRELOC)
	{										   /* no fixed register yet? */
		luaK_reserveregs(fs, 1);			   /* get a register */
		discharge2reg(fs, e, fs->freereg - 1); /* put value there */
	}
}

static int code_loadbool(FuncState *fs, int A, int b, int jump)
{
	luaK_getlabel(fs); /* those instructions may be jump targets */
	return luaK_codeABC(fs, OP_LOADBOOL, A, b, jump);
}

/**
 * check whether list has any jump that do not produce a value
 * or produce an inverted value
 * 只要一个跳转链表中 有一个指令不是OP_TESTSET，那么就返回true
**/
static int need_value(FuncState *fs, int list)
{
	for (; list != NO_JUMP; list = getjump(fs, list))
	{
		Instruction i = *getjumpcontrol(fs, list);
		if (GET_OPCODE(i) != OP_TESTSET)
			return 1;
	}
	return 0; /* not found */
}

/**
 * Ensures final expression result (including results from its jump
 * lists) is in register 'reg'.
 * If expression has jumps, need to patch these jumps either to
 * its final position or to "load" instructions (for those tests
 * that do not produce values).
**/
static void exp2reg(FuncState *fs, expdesc *e, int reg)
{
	discharge2reg(fs, e, reg);
	if (e->k == VJMP)					   /* expression itself is a test? */
		luaK_concat(fs, &e->t, e->u.info); /* put this jump in 't' list */

	//当前表达式是否需要跳转
	if (hasjumps(e))
	{
		/**
		 * 定义了3个位置变量
		 * 其中final用于保存整个表达式e最后的地址
		 * p_f/p_t分别用于保存false、 true跳转链表
		 */
		int final;		   /* position after whole expression */
		int p_f = NO_JUMP; /* position of an eventual LOAD false */
		int p_t = NO_JUMP; /* position of an eventual LOAD true */

		/**
		 * 分别传入表达式的truelist/falselist来调用need value函数，
		 * 只要其中一个 返回true，那么进入这个条件处理中。
		 * 函数need value的逻辑是，只要一个跳转链表中 有一个指令不是OP_TESTSET，那么就返回true
		 */
		if (need_value(fs, e->t) || need_value(fs, e->f))
		{
			/**根据当前表达式是不是VJMP类型，判断是否生成跳转指令。
			 * 很显然，即使生成了跳转指令，
			 * 当前也不知道跳转地址，需要在后面进行回填操作，
			 * 因此把这个指令存放在局部变量fj中了。
			**/
			int fj = (e->k == VJMP) ? NO_JUMP : luaK_jump(fs);

			/**
			 * 将地址地址分别存放在p_f和p_t 中。
			 * 同样需要注意的是，这两个地址当前也是不知道最终地址的，后面需要进行回填操作
			*/
			p_f = code_loadbool(fs, reg, 0, 1);
			p_t = code_loadbool(fs, reg, 1, 0);

			// 进行回填操作
			luaK_patchtohere(fs, fj);
		}
		/**
		 * 假如前面需要生成的额外指令都已经生成 final变量在这里可以拿到表达式e的后一个位置了。
		*/
		final = luaK_getlabel(fs);

		/**
		 * 调用patchlistaux函数，以final变量为目标地址，分别对表达式e的truelist和falselist进行回填操作。
		*/
		patchlistaux(fs, e->f, final, reg, p_f);
		patchlistaux(fs, e->t, final, reg, p_t);
	}

	e->f = e->t = NO_JUMP;
	e->u.info = reg;
	e->k = VNONRELOC;
}

/**
 * Ensures final expression result (including results from its jump
 * lists) is in next available register.
 * 根据expdesc这个结构体的信息来生成对应的字节码
*/
void luaK_exp2nextreg(FuncState *fs, expdesc *e)
{
	luaK_dischargevars(fs, e); //根据变量所在的不同作用域( local, global, upvalue ) 来决定这个变量是否需要重定向。
	freeexp(fs, e);
	luaK_reserveregs(fs, 1);		 //分配可用的函数寄存器空间,得到这个空间对应的寄存器索引。 有了空间,才能存储变量
	exp2reg(fs, e, fs->freereg - 1); //真正完成把表达式的数据放入寄存器空间的工作。 在这个函数中,最终又会调用discharge2reg函数,这个函数式根据不同的表达式类型( NIL,布尔表达式, 数字等)来生成存取表达式的值到寄存器的字节码。
}

/**
 * 返回寄存器索引
 * Ensures final expression result (including results from its jump
 * lists) is in some (any) register and return that register.
*/
int luaK_exp2anyreg(FuncState *fs, expdesc *e)
{
	luaK_dischargevars(fs, e);
	if (e->k == VNONRELOC)
	{						  /* expression already has a register? */
		if (!hasjumps(e))	  /* no jumps? */
			return e->u.info; /* result is already in a register */
		if (e->u.info >= fs->nactvar)
		{							   /* reg. is not a local? */
			exp2reg(fs, e, e->u.info); /* put final result in it */
			return e->u.info;
		}
	}
	luaK_exp2nextreg(fs, e); /* otherwise, use next available register */
	return e->u.info;
}

/*
** Ensures final expression result is either in a register or in an
** upvalue.
*/
void luaK_exp2anyregup(FuncState *fs, expdesc *e)
{
	if (e->k != VUPVAL || hasjumps(e))
		luaK_exp2anyreg(fs, e);
}

/*
** Ensures final expression result is either in a register or it is
** a constant.
*/
void luaK_exp2val(FuncState *fs, expdesc *e)
{
	if (hasjumps(e))
		luaK_exp2anyreg(fs, e);
	else
		luaK_dischargevars(fs, e);
}

/*
** Ensures final expression result is in a valid R/K index
** (that is, it is either in a register or in 'k' with an index
** in the range of R/K indices).
** Returns R/K index.
*/
int luaK_exp2RK(FuncState *fs, expdesc *e)
{
	luaK_exp2val(fs, e);
	switch (e->k)
	{ /* move constants to 'k' */
	case VTRUE:
		e->u.info = boolK(fs, 1);
		goto vk;
	case VFALSE:
		e->u.info = boolK(fs, 0);
		goto vk;
	case VNIL:
		e->u.info = nilK(fs);
		goto vk;
	case VKINT:
		e->u.info = luaK_intK(fs, e->u.ival);
		goto vk;
	case VKFLT:
		e->u.info = luaK_numberK(fs, e->u.nval);
		goto vk;
	case VK:
	vk:
		e->k = VK;
		if (e->u.info <= MAXINDEXRK) /* constant fits in 'argC'? */
			return RKASK(e->u.info);
		else
			break;
	default:
		break;
	}
	/* not a constant in the right range: put it in a register */
	return luaK_exp2anyreg(fs, e);
}

/**
 * uaK_storevar函数中,通过变量的类型,来区分不同的操作。主要分为:局部变量、全局变量、下标类型
 * 局部变量:主要调用exp2reg函数,该函数底层调用discharge2reg函数,通过值的不同类型,来实现不同的操作码生成操作
 * 全局变量:全局变量首先会调用luaK_exp2anyreg函数,实际底层也是调用了exp2reg函数,针对不同值类型进行不同的操作码封装操作。然后调用luaK_codeABC函数,进行OP_SETUPVAL全局变量的设置操作。
 * 下标类型:通过变量类型,来确定OP_SETTABLE或者OP_SETTABUP操作符,并调用luaK_codeABC函数进行操作码封装。
* Generate code to store result of expression 'ex' into variable 'var'.
*/
void luaK_storevar(FuncState *fs, expdesc *var, expdesc *ex)
{
	switch (var->k)
	{
	//局部变量,需要声明 local 标识
	case VLOCAL:
	{
		freeexp(fs, ex);
		//A=结果 B=变量
		exp2reg(fs, ex, var->u.info); /* compute 'ex' into proper place */
		return;
	}
		// Lua除了局部变量外,都是全局变量
	case VUPVAL:
	{
		int e = luaK_exp2anyreg(fs, ex);				  //底下也是调用exp2reg函数,主要用于将值设置到变量上
		luaK_codeABC(fs, OP_SETUPVAL, e, var->u.info, 0); //全局变量设置一下
		break;
	}
	//Table格式
	case VINDEXED:
	{
		OpCode op = (var->u.ind.vt == VLOCAL) ? OP_SETTABLE : OP_SETTABUP;
		int e = luaK_exp2RK(fs, ex);
		luaK_codeABC(fs, op, var->u.ind.t, var->u.ind.idx, e);
		break;
	}
	default:
		lua_assert(0); /* invalid var kind to store */
	}
	freeexp(fs, ex);
}

/*
** Emit SELF instruction (convert expression 'e' into 'e:key(e,').
*/
void luaK_self(FuncState *fs, expdesc *e, expdesc *key)
{
	int ereg;
	luaK_exp2anyreg(fs, e);
	ereg = e->u.info; /* register where 'e' was placed */
	freeexp(fs, e);
	e->u.info = fs->freereg; /* base register for op_self */
	e->k = VNONRELOC;		 /* self expression has a fixed register */
	luaK_reserveregs(fs, 2); /* function and 'self' produced by op_self */
	luaK_codeABC(fs, OP_SELF, e->u.info, ereg, luaK_exp2RK(fs, key));
	freeexp(fs, key);
}

/*
** Negate condition 'e' (where 'e' is a comparison).
*/
static void negatecondition(FuncState *fs, expdesc *e)
{
	Instruction *pc = getjumpcontrol(fs, e->u.info);
	lua_assert(testTMode(GET_OPCODE(*pc)) && GET_OPCODE(*pc) != OP_TESTSET &&
			   GET_OPCODE(*pc) != OP_TEST);
	SETARG_A(*pc, !(GETARG_A(*pc)));
}

/*
** Emit instruction to jump if 'e' is 'cond' (that is, if 'cond'
** is true, code will jump if 'e' is true.) Return jump position.
** Optimize when 'e' is 'not' something, inverting the condition
** and removing the 'not'.
*/
static int jumponcond(FuncState *fs, expdesc *e, int cond)
{
	if (e->k == VRELOCABLE)
	{
		Instruction ie = getinstruction(fs, e);
		if (GET_OPCODE(ie) == OP_NOT)
		{
			fs->pc--; /* remove previous OP_NOT */
			return condjump(fs, OP_TEST, GETARG_B(ie), 0, !cond);
		}
		/* else go through */
	}
	discharge2anyreg(fs, e);
	freeexp(fs, e);
	return condjump(fs, OP_TESTSET, NO_REG, e->u.info, cond);
}

/**
 * Emit code to go through if 'e' is true, jump otherwise.
*/
void luaK_goiftrue(FuncState *fs, expdesc *e)
{
	int pc; /* pc of new jump */

	//调用函数将传人的表达式解析出来
	luaK_dischargevars(fs, e);

	//根据解析出来的表达式类型做不同的处理
	switch (e->k)
	{
		/**
		 * 如果是VJMP，则说明表达式V是一个逻辑类指令，
		 * 这时需要将它的跳转条件进行颠倒操作。
		 * 比如，如果前面的表达式是比较变量A是否等于变量B，
		 * 那么这里会被改写成变量A是否不等于变量B
		*/
	case VJMP:
	{							/* condition? */
		negatecondition(fs, e); /* jump when it is false */
		pc = e->u.info;			/* save jump position */
		break;
	}
		//当表达式是常量(VK)、 VKFLT,VKINT(数字)以及VTRUE布尔类型的true)时，并不需要增加一个跳转指令跳过下一条指令
	case VK:
	case VKFLT:
	case VKINT:
	case VTRUE:
	{
		pc = NO_JUMP; /* always true; do nothing */
		break;
	}
	/**
	 * 默认情况，此时需要进入jumponcond函数中，
	 * 生成针对表达 式V为false情况的OP_TESTSET指令。
	 * 注意，这里传入jumponcond函数中的cond参数是0,
	 * 也就是生成的是表达式为false情况下的指令
	*/
	default:
	{
		pc = jumponcond(fs, e, 0); /* jump when false */
		break;
	}
	}

	/**
	 * 前面根据表达式的不同类型生成跳转指令，
	 * 该指令的地址返回在局部变量pc中。
	 * 可以看到， pc可能有两种情况，
	 * 一种为NO_JUMP ，这种情况是表达式恒为true的情况，
	 * 其他情况最终都会生成跳转指令，
	 * 而这些跳转都发生在表达式V为false的情况。
	 * 因此，这里将返回的pc变量加入到表达式的falselist中
	*/
	luaK_concat(fs, &e->f, pc); /* insert new jump in false list */

	/**
	 * 将表达式的truelist加入到jpc跳转链表中。
	 * 前面已经分析过了，这在生成下一条指令时将下一条指令的pc遍历jpc链表进行回填操作。
	 * 换言之，表达式E为true的情况将跳转到前面生成的跳转指令的下一条指令
	*/
	luaK_patchtohere(fs, e->t); /* true list jumps to here (to go through) */
	
	e->t = NO_JUMP;
}

/*
** Emit code to go through if 'e' is false, jump otherwise.
*/
void luaK_goiffalse(FuncState *fs, expdesc *e)
{
	int pc; /* pc of new jump */
	luaK_dischargevars(fs, e);
	switch (e->k)
	{
	case VJMP:
	{
		pc = e->u.info; /* already jump if true */
		break;
	}
	case VNIL:
	case VFALSE:
	{
		pc = NO_JUMP; /* always false; do nothing */
		break;
	}
	default:
	{
		pc = jumponcond(fs, e, 1); /* jump if true */
		break;
	}
	}
	luaK_concat(fs, &e->t, pc); /* insert new jump in 't' list */
	luaK_patchtohere(fs, e->f); /* false list jumps to here (to go through) */
	e->f = NO_JUMP;
}

/*
** Code 'not e', doing constant folding.
*/
static void codenot(FuncState *fs, expdesc *e)
{
	luaK_dischargevars(fs, e);
	switch (e->k)
	{
	case VNIL:
	case VFALSE:
	{
		e->k = VTRUE; /* true == not nil == not false */
		break;
	}
	case VK:
	case VKFLT:
	case VKINT:
	case VTRUE:
	{
		e->k = VFALSE; /* false == not "x" == not 0.5 == not 1 == not true */
		break;
	}
	case VJMP:
	{
		negatecondition(fs, e);
		break;
	}
	case VRELOCABLE:
	case VNONRELOC:
	{
		discharge2anyreg(fs, e);
		freeexp(fs, e);
		e->u.info = luaK_codeABC(fs, OP_NOT, 0, e->u.info, 0);
		e->k = VRELOCABLE;
		break;
	}
	default:
		lua_assert(0); /* cannot happen */
	}
	/* interchange true and false lists */
	{
		int temp = e->f;
		e->f = e->t;
		e->t = temp;
	}
	removevalues(fs, e->f); /* values are useless when negated */
	removevalues(fs, e->t);
}

/*
** Create expression 't[k]'. 't' must have its final result already in a
** register or upvalue.
*/
void luaK_indexed(FuncState *fs, expdesc *t, expdesc *k)
{
	lua_assert(!hasjumps(t) && (vkisinreg(t->k) || t->k == VUPVAL));
	t->u.ind.t = t->u.info;			   /* register or upvalue index */
	t->u.ind.idx = luaK_exp2RK(fs, k); /* R/K index for key */
	t->u.ind.vt = (t->k == VUPVAL) ? VUPVAL : VLOCAL;
	t->k = VINDEXED;
}

/*
** Return false if folding can raise an error.
** Bitwise operations need operands convertible to integers; division
** operations cannot have 0 as divisor.
*/
static int validop(int op, TValue *v1, TValue *v2)
{
	switch (op)
	{
	case LUA_OPBAND:
	case LUA_OPBOR:
	case LUA_OPBXOR:
	case LUA_OPSHL:
	case LUA_OPSHR:
	case LUA_OPBNOT:
	{ /* conversion errors */
		lua_Integer i;
		return (tointeger(v1, &i) && tointeger(v2, &i));
	}
	case LUA_OPDIV:
	case LUA_OPIDIV:
	case LUA_OPMOD: /* division by 0 */
		return (nvalue(v2) != 0);
	default:
		return 1; /* everything else is valid */
	}
}

/*
** Try to "constant-fold" an operation; return 1 iff successful.
** (In this case, 'e1' has the final result.)
*/
static int constfolding(FuncState *fs, int op, expdesc *e1,
						const expdesc *e2)
{
	TValue v1, v2, res;
	if (!tonumeral(e1, &v1) || !tonumeral(e2, &v2) || !validop(op, &v1, &v2))
		return 0;							   /* non-numeric operands or not safe to fold */
	luaO_arith(fs->ls->L, op, &v1, &v2, &res); /* does operation */
	if (ttisinteger(&res))
	{
		e1->k = VKINT;
		e1->u.ival = ivalue(&res);
	}
	else
	{ /* folds neither NaN nor 0.0 (to avoid problems with -0.0) */
		lua_Number n = fltvalue(&res);
		if (luai_numisnan(n) || n == 0)
			return 0;
		e1->k = VKFLT;
		e1->u.nval = n;
	}
	return 1;
}

/*
** Emit code for unary expressions that "produce values"
** (everything but 'not').
** Expression to produce final result will be encoded in 'e'.
*/
static void codeunexpval(FuncState *fs, OpCode op, expdesc *e, int line)
{
	int r = luaK_exp2anyreg(fs, e); /* opcodes operate only on registers */
	freeexp(fs, e);
	e->u.info = luaK_codeABC(fs, op, 0, r, 0); /* generate opcode */
	e->k = VRELOCABLE;						   /* all those operations are relocatable */
	luaK_fixline(fs, line);
}

/*
** Emit code for binary expressions that "produce values"
** (everything but logical operators 'and'/'or' and comparison
** operators).
** Expression to produce final result will be encoded in 'e1'.
** Because 'luaK_exp2RK' can free registers, its calls must be
** in "stack order" (that is, first on 'e2', which may have more
** recent registers to be released).
*/
static void codebinexpval(FuncState *fs, OpCode op,
						  expdesc *e1, expdesc *e2, int line)
{
	int rk2 = luaK_exp2RK(fs, e2); /* both operands are "RK" */
	int rk1 = luaK_exp2RK(fs, e1);
	freeexps(fs, e1, e2);
	e1->u.info = luaK_codeABC(fs, op, 0, rk1, rk2); /* generate opcode */
	e1->k = VRELOCABLE;								/* all those operations are relocatable */
	luaK_fixline(fs, line);
}

/**
 * Emit code for comparisons.
 * 'e1' was already put in R/K form by 'luaK_infix'.
 * 处理关系指令
**/
static void codecomp(FuncState *fs, BinOpr opr, expdesc *e1, expdesc *e2)
{
	//将进行比较的表达式加载到RK数组中
	int rk1 = (e1->k == VK) ? RKASK(e1->u.info)
							: check_exp(e1->k == VNONRELOC, e1->u.info);
	int rk2 = luaK_exp2RK(fs, e2);

	//因为前面已经加载了表达式的值，这里释放这两个表达式占用的空间。
	freeexps(fs, e1, e2);

	switch (opr)
	{
	case OPR_NE:
	{ /* '(a ~= b)' ==> 'not (a == b)' */
		e1->u.info = condjump(fs, OP_EQ, 0, rk1, rk2);
		break;
	}
	case OPR_GT:
	case OPR_GE:
	{
		/* '(a > b)' ==> '(b < a)';  '(a >= b)' ==> '(b <= a)' */
		OpCode op = cast(OpCode, (opr - OPR_NE) + OP_EQ);
		e1->u.info = condjump(fs, op, 1, rk2, rk1); /* invert operands */
		break;
	}
	default:
	{ /* '==', '<', '<=' use their own opcodes */
		OpCode op = cast(OpCode, (opr - OPR_EQ) + OP_EQ);
		e1->u.info = condjump(fs, op, 1, rk1, rk2);
		break;
	}
	}
	e1->k = VJMP;
}

/*
** Apply prefix operation 'op' to expression 'e'.
*/
void luaK_prefix(FuncState *fs, UnOpr op, expdesc *e, int line)
{
	static const expdesc ef = {VKINT, {0}, NO_JUMP, NO_JUMP};
	switch (op)
	{
	case OPR_MINUS:
	case OPR_BNOT: /* use 'ef' as fake 2nd operand */
		if (constfolding(fs, op + LUA_OPUNM, e, &ef))
			break;
		/* FALLTHROUGH */
	case OPR_LEN:
		codeunexpval(fs, cast(OpCode, op + OP_UNM), e, line);
		break;
	case OPR_NOT:
		codenot(fs, e);
		break;
	default:
		lua_assert(0);
	}
}

/*
** Process 1st operand 'v' of binary operation 'op' before reading
** 2nd operand.
*/
void luaK_infix(FuncState *fs, BinOpr op, expdesc *v)
{
	switch (op)
	{
	case OPR_AND:
	{
		luaK_goiftrue(fs, v); /* go ahead only if 'v' is true */
		break;
	}
	case OPR_OR:
	{
		luaK_goiffalse(fs, v); /* go ahead only if 'v' is false */
		break;
	}
	case OPR_CONCAT:
	{
		luaK_exp2nextreg(fs, v); /* operand must be on the 'stack' */
		break;
	}
	case OPR_ADD:
	case OPR_SUB:
	case OPR_MUL:
	case OPR_DIV:
	case OPR_IDIV:
	case OPR_MOD:
	case OPR_POW:
	case OPR_BAND:
	case OPR_BOR:
	case OPR_BXOR:
	case OPR_SHL:
	case OPR_SHR:
	{
		if (!tonumeral(v, NULL))
			luaK_exp2RK(fs, v);
		/* else keep numeral, which may be folded with 2nd operand */
		break;
	}
	default:
	{
		luaK_exp2RK(fs, v);
		break;
	}
	}
}

/*
** Finalize code for binary operation, after reading 2nd operand.
** For '(a .. b .. c)' (which is '(a .. (b .. c))', because
** concatenation is right associative), merge second CONCAT into first
** one.
*/
void luaK_posfix(FuncState *fs, BinOpr op,
				 expdesc *e1, expdesc *e2, int line)
{
	switch (op)
	{
	case OPR_AND:
	{
		lua_assert(e1->t == NO_JUMP); /* list closed by 'luK_infix' */
		luaK_dischargevars(fs, e2);
		luaK_concat(fs, &e2->f, e1->f);
		*e1 = *e2;
		break;
	}
	case OPR_OR:
	{
		lua_assert(e1->f == NO_JUMP); /* list closed by 'luK_infix' */
		luaK_dischargevars(fs, e2);
		luaK_concat(fs, &e2->t, e1->t);
		*e1 = *e2;
		break;
	}
	case OPR_CONCAT:
	{
		luaK_exp2val(fs, e2);
		if (e2->k == VRELOCABLE &&
			GET_OPCODE(getinstruction(fs, e2)) == OP_CONCAT)
		{
			lua_assert(e1->u.info == GETARG_B(getinstruction(fs, e2)) - 1);
			freeexp(fs, e1);
			SETARG_B(getinstruction(fs, e2), e1->u.info);
			e1->k = VRELOCABLE;
			e1->u.info = e2->u.info;
		}
		else
		{
			luaK_exp2nextreg(fs, e2); /* operand must be on the 'stack' */
			codebinexpval(fs, OP_CONCAT, e1, e2, line);
		}
		break;
	}
	case OPR_ADD:
	case OPR_SUB:
	case OPR_MUL:
	case OPR_DIV:
	case OPR_IDIV:
	case OPR_MOD:
	case OPR_POW:
	case OPR_BAND:
	case OPR_BOR:
	case OPR_BXOR:
	case OPR_SHL:
	case OPR_SHR:
	{
		if (!constfolding(fs, op + LUA_OPADD, e1, e2))
			codebinexpval(fs, cast(OpCode, op + OP_ADD), e1, e2, line);
		break;
	}
	case OPR_EQ:
	case OPR_LT:
	case OPR_LE:
	case OPR_NE:
	case OPR_GT:
	case OPR_GE:
	{
		codecomp(fs, op, e1, e2);
		break;
	}
	default:
		lua_assert(0);
	}
}

/*
** Change line information associated with current position.
*/
void luaK_fixline(FuncState *fs, int line)
{
	fs->f->lineinfo[fs->pc - 1] = line;
}

/*
** Emit a SETLIST instruction.
** 'base' is register that keeps table;
** 'nelems' is #table plus those to be stored now;
** 'tostore' is number of values (in registers 'base + 1',...) to add to
** table (or LUA_MULTRET to add up to stack top).
*/
void luaK_setlist(FuncState *fs, int base, int nelems, int tostore)
{
	int c = (nelems - 1) / LFIELDS_PER_FLUSH + 1;
	int b = (tostore == LUA_MULTRET) ? 0 : tostore;
	lua_assert(tostore != 0 && tostore <= LFIELDS_PER_FLUSH);
	if (c <= MAXARG_C)
		luaK_codeABC(fs, OP_SETLIST, base, b, c);
	else if (c <= MAXARG_Ax)
	{
		luaK_codeABC(fs, OP_SETLIST, base, b, 0);
		codeextraarg(fs, c);
	}
	else
		luaX_syntaxerror(fs->ls, "constructor too long");
	fs->freereg = base + 1; /* free registers with list values */
}
