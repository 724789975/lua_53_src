/*
** $Id: lobject.h,v 2.117.1.1 2017/04/19 17:39:34 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	LUA_NUMTAGS		/* function prototypes */
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)		/* removed keys in tables */

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/


/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


/* Variant tags for strings */
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */


/* Variant tags for numbers */
#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))  /* float numbers */
#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))  /* integer numbers */


/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
typedef struct GCObject GCObject;


/**
 * Common Header for all collectable objects (in macro form, to be
 * included in other objects)
 * marked是在垃圾回收过程中用以标记对象存活状态
 * 其中0和1位用来表示对象的white状态和垃圾状态。
 * 当垃圾回收的标识阶段结束后，剩下的white对象就是垃圾对象。
 * 由于lua并不是立即清除这些垃圾对象，而是一步步逐渐清除，所以这些对象还会在系统中存在一段时间。
 * 这就需要我们能够区分出同样为white状态的垃圾对象和非垃圾对象。Lua使用两个标志位来表示white，就是为了高效的解决这个问题。
 * 这个标志位会轮流被当作white状态标志，另一个表示垃圾状态。
 * 在global_State中保存着一个currentwhite，来表示当前是那个标志位用来标识white。
 * 每当GC标识阶段完成，系统会切换这个标志位，这样原来为white的所有对象不需要遍历就变成了垃圾对象，而真正的white对象则使用新的标志位标识。
 * 
 * 第2个标志位用来表示black状态，而既非white也非black就是gray状态。
 * 除了short string和open upvalue之外，所有的GCObject都通过next被串接到全局状态global_State中的allgc链表上。
 * 我们可以通过遍历allgc链表来访问系统中的所有GCObject。short string被字符串标单独管理。open upvalue会在被close时也连接到allgc上。
*/
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};




/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/*
** Union of all Lua values
** lua 中的数据可以这样分为两类：值类型和引用类型
** 值类型可以被任意复制，而引用类型共享一份数据，由 GC 负责维护生命期
** lua 使用一个联合 union Value 来保存数据
*/
typedef union Value {
  GCObject *gc;    /* 存放所有需要垃圾回收的类型的对象 closure(lua closure+C closure), string, userdata, table, thread, collectable objects */
  void *p;         /* 存放轻量用户数据类型(lightuserdata) light userdata */
  int b;           /* booleans */
  lua_CFunction f; /* 存放一个方法 light C functions */
  lua_Integer i;   /* 存放int数字 integer numbers */
  lua_Number n;    /* 存放float数字 float numbers */
} Value;


/*
** 引用类型用一个指针 GCObject *gc 来间接引用，而其它值类型都直接保存在联合中
** 为了区分联合中存放的数据类型，再额外绑定一个类型字段
** tt_是一个8 bits 的类型标记字段，被分成3个部分：
** 0-3位，表示大类型
** 4-5位，表示子类型
** 第6位，表示是否可以垃圾回收
*/
#define TValuefields	Value value_; int tt_
typedef struct lua_TValue {
  TValuefields;
} TValue;



/* macro defining a nil value */
#define NILCONSTANT	{NULL}, LUA_TNIL


#define val_(o)		((o)->value_)


/* raw type tag of a TValue */
#define rttype(o)	((o)->tt_)

/* tag with no variants (bits 0-3) */
#define novariant(x)	((x) & 0x0F)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define ttype(o)	(rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3) */
#define ttnov(o)	(novariant(rttype(o)))


/* Macros to test type */
/*判断是否是具体的数据类型*/
#define checktag(o,t)		(rttype(o) == (t))
#define checktype(o,t)		(ttnov(o) == (t))
#define ttisnumber(o)		checktype((o), LUA_TNUMBER)
#define ttisfloat(o)		checktag((o), LUA_TNUMFLT)
#define ttisinteger(o)		checktag((o), LUA_TNUMINT)
#define ttisnil(o)		checktag((o), LUA_TNIL)
#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		checktype((o), LUA_TSTRING)
#define ttisshrstring(o)	checktag((o), ctb(LUA_TSHRSTR))
#define ttislngstring(o)	checktag((o), ctb(LUA_TLNGSTR))
#define ttistable(o)		checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		checktype(o, LUA_TFUNCTION)
#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		checktag((o), LUA_TLCF)
#define ttisfulluserdata(o)	checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)


/* Macros to access values */
/*获得具体数据类型的值*/
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)

#define checkliveness(L,obj) \
	lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))


/* Macros to set values */
/*设置具体数据类型的值*/
#define settt_(o,t)	((o)->tt_=(t))

#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }

#define chgfltvalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }

#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }

#define chgivalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }

#define setnilvalue(obj) settt_(obj, LUA_TNIL)

#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }

#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(L,io); }

#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(L,io); }

#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(L,io); }

#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(L,io); }

#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(L,io); }

#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)



#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(L,io1); }


/*
** different types of assignments, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* to table (define it as an expression to be used in macros) */
#define setobj2t(L,o1,o2)  ((void)L, *(o1)=*(o2), checkliveness(L,(o1)))




/*
** {======================================================
** types and prototypes
** =======================================================
*/

/*
** lua_state 的数据栈，就是一个 TValue 的数组
** 代码中用 StkId 类型来指代对 TValue 的引用
*/
typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
** 字符串结构
** 真正的字符串的内容，直接存储在结构体后面的内存里
** 为了保证内存的对齐，对TString和基本类型合并做一个字节对齐
**
** Lua字符串对象 = TString结构 + 实际字符串数据
** TString结构 = GCObject *指针 + 字符串信息数据
*/
typedef struct TString {
  CommonHeader;
  /**
   * 用于记录辅助信息
   * 对于短字符串，该字段用来标记字符串是否为保留字，用于词法分析器中对保留字的快速判断；
   * 对于长字符串，该字段将用于惰性求哈希值的策略（第一次用到才进行哈希）
   * , reserved words for short strings; "has hash" for longs */
  lu_byte extra;  
  
  lu_byte shrlen;  /* 由于Lua并不以'\0'字符结尾来识别字符串的长度，因此需要一个len域来记录其长度 length for short strings */
  unsigned int hash; /*记录字符串的hash值，可以用来加快字符串的匹配和查找*/
  union {
    size_t lnglen;  /* 长字符串存储形式 length for long strings */
    /**
     * hash table中相同hash值的字符串将串成一个列表，
     * hnext域为指向下一个列表节点的指针，
     * 短字符串用到 linked list for hash table
     */
    struct TString *hnext;
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))

/* get string length from 'TString *s' */
#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o' */
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
** metatable是每个实例一个
** user_和ttuv_两个字段则是值部分
*/
typedef struct Udata {
  CommonHeader;
  lu_byte ttuv_;  /* 类型标记字段: 标记的是该UserData里实际存储的值(user_字段)的类型 user value's tag */
  struct Table *metatable;
  size_t len;  /* 定义了实际的数据长度 number of bytes */
  union Value user_;  /* user value */
} Udata;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }


#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes
** 描述闭包变量的信息
*/
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;//变量名
  int startpc;  /* 作用域 first point where variable is active */
  int endpc;    /* 作用域 first point where variable is dead */
} LocVar;


/*
** Function Prototypes
** 一个Lua 闭包
*/
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* 函数参数个数 number of fixed parameters */
  lu_byte is_vararg;  /*是否是有变长参数*/
  lu_byte maxstacksize;  /* 最大的函数栈长度 number of registers needed by this function */
  int sizeupvalues;  /* 闭包变量数组长度 size of 'upvalues' */
  int sizek;  /* 常量数组长度 size of 'k' */
  int sizecode;/*指令数组的长度*/
  int sizelineinfo;/*行信息数组长度*/
  int sizep;  /* 嵌套的Proto数组长度 size of 'p' */
  int sizelocvars;/*局部变量数组长度*/
  int linedefined;  /* 函数的起始定义行号 debug information  */
  int lastlinedefined;  /* 函数的起始定义行号 debug information  */
  TValue *k;  /* 常量数组 constants used by the function */
  Instruction *code;  /* 三地址指令数组 opcodes */
  struct Proto **p;  /* 嵌套的Proto数组 functions defined inside the function */
  int *lineinfo;  /* 行信息数组 map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* 局部变量数组 information about local variables (debug information) */
  Upvaldesc *upvalues;  /* 闭包变量数组 upvalue information */
  struct LClosure *cache;  /* 缓存嵌套的Proto的闭包 last-created closure with this prototype */
  TString  *source;  /* 源码字符串 used for debug information */
  GCObject *gclist;/*垃圾回收专用*/
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;


/*
** Closures
*/

/*
** nupvalues说明闭包变量的个数
** gclist用以垃圾回收
*/
#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

typedef union TKey {
  struct {
    TValuefields;
    int next;  /* 链表 管理Hash Node，用于处理hash 冲突 for chaining (offset for next node) */
  } nk;
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next' */
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }


/**
 * 节点格式 k=>v
 * 节点结构：a = {x=12,mutou=99,[3]="hello"}
 */
typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;

/**
 * Table数据结构，分两种存储类型：数组节点和hash节点
 * 数组节点：sizearray为数字长度，一般存储key值在长度范围内的结果集
 * hash节点：k=>v结构，能够存储各类复杂对象结构
 *
 * LUA语言用法：
 * 数组节点：fruits = {"banana","orange","apple"}
 * hash节点：a = {x=12,mutou=99,[3]="hello"}
 * table中带table结构：local a = {{x = 1,y=2},{x = 3,y = 10}}
 */
typedef struct Table {
  CommonHeader;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;  /* 节点个数 log2 of size of 'node' array */
  unsigned int sizearray;  /* size of 'array' array */
  TValue *array;  /* 数组方式 array part */
  Node *node; //hash节点，node指向hash表的起始位置
  Node *lastfree;  /* hash节点，最后一个空闲节点 any free position is before this position */
  struct Table *metatable;  //元表，重载操作需要用
  GCObject *gclist;//用以垃圾回收的
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, StkId obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

