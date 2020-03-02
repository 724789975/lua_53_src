/*
** $Id: lstate.h,v 2.133.1.1 2017/04/19 17:39:34 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).
**
** Moreover, there is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray:
**
** 'gray': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all kinds of weak tables during propagation phase;
**   - all threads.
** 'weak': tables with weak values to be cleared;
** 'ephemeron': ephemeron tables with white->white entries;
** 'allweak': tables with weak keys and/or weak values to be cleared.
** The last three lists are used only during the atomic phase.

*/


struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */


typedef struct stringtable {
  TString **hash;
  int nuse;  /* number of elements */
  int size;
} stringtable;


/*
** Information about a call.
** When a thread yields, 'func' is adjusted to pretend that the
** top function has only the yielded values in its stack; in that
** case, the actual 'func' value is saved in field 'extra'.
** When a function calls another with a continuation, 'extra' keeps
** the function index so that, in case of errors, the continuation
** function can be called with the correct top.
拆解为以下结构
typedef struct LuaCallInfo  {
  // DataStack  [base,...,top]
  StackId base; //数据栈
  StkId top;   //数据栈

  // Closure
  StkId func;   // Lua Closure 里面包含了lua Proto(=指令+参数+局部变量+常量+内嵌函数..)
  ptrdiff_t extra;
  
  // Code
  const Instruction* savedpc;//当前执行的指令

  // Call Result
  lu_byte callstatus;//调用后的结果
  short nresults;//描述返回结果的个数，便于在执行结束的时候调整top

  // Call link
  struct CallInfo *previous, *next; //串起动态增减的CallStack
}

C CallInfo并不需要DataStack的base，只需要记住数据栈栈顶即可。
func的里面就是


typdef struct CCallInfo  {
  // Data
  StkId top;  

  // Closure
  
  //一个CClosure(lua_CFunction+闭包的TValues数组，代码和数据都简单多了）
  //执行的过程也别Lua CallInfo简单多了，直接调用CClosure里面的lua_CFunction即可
  StkId func;   

  ptrdiff_t extra;

  // Call Result
  lu_byte callstatus;
  short nresults;

  // Error Recover
  ptrdiff_t old_errfunc;//C函数的执行超出了Lua的控制范围，每一层执行都需要有一个old_errfunc，用以错误处理

  // Continuation(or Callback)
  lua_KFunction k;
  lua_KContext ctx;

  // Call link
  struct CallInfo *previous, *next; 
}
*/
typedef struct CallInfo {
  StkId func;  /* 当前调用栈的调用指针处 function index in the stack */
  StkId	top;  /* 调用栈的栈顶 top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lua_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  ptrdiff_t extra;/*在执行过程中临时保持func用的*/
  short nresults;  /* expected number of results from this function */
  unsigned short callstatus;
} CallInfo;


/*
** Bits in CallInfo status
*/
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<2)	/* call is running a debug hook */
#define CIST_FRESH	(1<<3)	/* call is running on a fresh invocation
                            //       of luaV_execute */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_LEQ	(1<<7)  /* using __lt for __le */
#define CIST_FIN	(1<<8)  /* call is running a finalizer */

#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
** lua 全局状态机
** 作用：管理全局数据，全局字符串表、内存管理函数、 GC 把所有对象串联起来的信息、内存等
*/
typedef struct global_State {
  lua_Alloc frealloc;  /* Lua的全局内存分配器，用户可以替换成自己的 function to reallocate memory */
  void *ud;         /*分配器的userdata auxiliary data to 'frealloc' */
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  stringtable strt;  /* 字符串table Lua的字符串分短字符串和长字符串 hash table for strings */
  TValue l_registry;  //Lua注册表
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcrunning;  /* true if GC is running */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  GCObject *fixedgc;  /* list of objects not to be collected */
  struct lua_State *twups;  /* 闭包了当前线程变量的其他线程列表 list of threads with open upvalues */
  unsigned int gcfinnum;  /* number of finalizers to call in each GC step */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC 'granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct lua_State *mainthread; /* 主线程 */
  const lua_Number *version;  /*  版本号 pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /*metatable的预定义方法名字数组，tm是tag method的缩写 array with tag-method names */
  /*
  每个基本类型一个metatable
  注意table、userdata等则是每个实例一个metatable
  metatable+tag method可以说是整个Lua最重要的Hook机制
  metatables for basic types
  */
  struct Table *mt[LUA_NUMTAGS];
  TString *strcache[STRCACHE_N][STRCACHE_M];  /*字符串缓存 cache for strings in API */
} global_State;


/*
** 'per thread' state
** Lua 主线程 栈 数据结构
** 作用：管理整个栈和当前函数使用的栈的情况，最主要的功能就是函数调用以及和c的通信
*/
struct lua_State {
  CommonHeader;
  unsigned short nci;  /* number of items in 'ci' list 存储一共多少个CallInfo */
  lu_byte status;/* 解析容器的用于记录中间状态*/
  StkId top;  /*线程栈的栈顶指针 first free slot in the stack */
  global_State *l_G;/* 这个是Lua的全局对象，所有的lua_State共享一个global_State，global_State里塞进了各种全局字段 */
  CallInfo *ci;  /*当前运行函数信息 call info for current function */
  const Instruction *oldpc;  /*在当前thread 的解释执行指令的过程中，指向最后一次执行的指令的指针 last pc traced */
  StkId stack_last;  /* 线程栈的最后一个位置 last free slot in the stack */
  StkId stack;  /* 栈的指针，当前执行的位置 stack base */
  /*
  从CallStack的栈底到栈顶的所有open的UpVal也构成了一种Stack
  Lua把这些open状态的UpVal用链表串在一起
  我们可以认为是一个open upvalue stack
  这个stack的栈底就是UpVal* openval
  list of open upvalues in this stack
  */
  UpVal *openupval;
  GCObject *gclist;/* GC列表 */
  struct lua_State *twups;  /* 那些闭包了当前lua_State的变量的其他协程 list of threads with open upvalues */
  struct lua_longjmp *errorJmp;  /* current error recover point */
  CallInfo base_ci;  /*调用栈的头部指针  CallInfo for first level (C calling Lua) */
  volatile lua_Hook hook;
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  int stacksize;
  int basehookcount;
  int hookcount;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* 记录CallStack动态增减过程中调用的C函数的个数 number of nested C calls */
  l_signalT hookmask;
  lu_byte allowhook;
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
};


#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


/* macro to convert a Lua object into a GCObject */
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

