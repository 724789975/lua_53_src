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


/**
 * Upvalues for Lua closures
 * Upvalue对象在垃圾回收中的处理是比较特殊的。
 * 对于open状态的upvalue,其v指向的是一个stack上的TValue,所以open upvalue与thread的关系非常紧密。
 * 引用到open upvalue的只可能是其从属的thread,以及lua closure。
 * 如果没有lua closure引用这个open upvalue,就算他一定被thread引用着,也已经没有实际的意义了,应该被回收掉。
 * 也就是说thread对open upvalue的引用完全是一个弱引用。
 * 所以Lua没有将open upvalue当作一个独立的可回收对象,而是将其清理工作交给从属的thread对象来完成。
 * 在mark过程中,open upvalue对象只使用white和gray两个状态,来代表是否被引用到。
 * 通过上面的引用关系可以看到,有可能引用open upvalue的对象只可能被lua closure引用到。
 * 所以一个gray的open upvalue就代表当前有lua closure正在引用他,而这个lua closure不一定在这个thread的stack上面。
 * 在清扫阶段,thread对象会遍历所有从属于自己的open upvalue。
 * 如果不是gray,就说明当前没有lua closure引用这个open upvalue了,可以被销毁。
 * 当退出upvalue的语法域或者thread被销毁,open upvalue会被close。
 * 所有close upvalue与thread已经没有弱引用关系,会被转化为一个普通的可回收对象,和其他对象一样进行独立的垃圾回收。
*/
struct UpVal {
  TValue *v;  /* 指向了闭包变量的真正的值的指针 points to stack or to its own value */
  lu_mem refcount;  /* 被闭包的引用计数 reference counter */

  /*一个Proto在外层函数没有返回之前
  ** 处于open状态,闭包的变量,直接通过UpVal ->v这个指针引用
  ** 此时open结构用来把当前作用域内的所有闭包变量都串起来做成一个链表,方便查找
  ** 此时u->value并没有用到
  */
  union {
    struct {  /* (when open) */
      UpVal *next;  /* linked list */
      int touched;  /* mark to avoid cycles with dead threads */
    } open;

    /*
    ** 如果外层函数返回,则Proto需要把闭包变量的值拷贝出来,保证对象安全
    ** 这个拷贝就放在u->value里
    ** 此时,UpVal ->v也直接指向内部的u->value
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
