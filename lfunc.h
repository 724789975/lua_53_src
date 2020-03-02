/*
** $Id: lfunc.h,v 2.15.1.1 2017/04/19 17:39:34 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


#define sizeCclosure(n)	(cast(int, sizeof(CClosure)) + \
                         cast(int, sizeof(TValue)*((n)-1)))

#define sizeLclosure(n)	(cast(int, sizeof(LClosure)) + \
                         cast(int, sizeof(TValue *)*((n)-1)))


/* test whether thread is in 'twups' list */
#define isintwups(L)	(L->twups != L)


/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in a VM register.)
*/
#define MAXUPVAL	255


/*
** Upvalues for Lua closures
*/
struct UpVal {
  TValue *v;  /* 指向了闭包变量的真正的值的指针 points to stack or to its own value */
  lu_mem refcount;  /* 被闭包的引用计数 reference counter */

  /*一个Proto在外层函数没有返回之前
  ** 处于open状态，闭包的变量，直接通过UpVal ->v这个指针引用
  ** 此时open结构用来把当前作用域内的所有闭包变量都串起来做成一个链表，方便查找
  ** 此时u->value并没有用到
  */
  union {
    struct {  /* (when open) */
      UpVal *next;  /* linked list */
      int touched;  /* mark to avoid cycles with dead threads */
    } open;

    /*
    ** 如果外层函数返回，则Proto需要把闭包变量的值拷贝出来，保证对象安全
    ** 这个拷贝就放在u->value里
    ** 此时，UpVal ->v也直接指向内部的u->value
    */
    TValue value;  /* the value (when closed) */
  } u;
};

/*
** 我们可以通过判断UpVal ->v和u->value是否相等来判断UpVal处于open还是clsoed状态
*/
#define upisopen(up)	((up)->v != &(up)->u.value)


LUAI_FUNC Proto *luaF_newproto (lua_State *L);
LUAI_FUNC CClosure *luaF_newCclosure (lua_State *L, int nelems);
LUAI_FUNC LClosure *luaF_newLclosure (lua_State *L, int nelems);
LUAI_FUNC void luaF_initupvals (lua_State *L, LClosure *cl);
LUAI_FUNC UpVal *luaF_findupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_close (lua_State *L, StkId level);
LUAI_FUNC void luaF_freeproto (lua_State *L, Proto *f);
LUAI_FUNC const char *luaF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
