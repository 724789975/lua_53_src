/*
** $Id: lgc.h,v 2.91.1.1 2017/04/19 17:39:34 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/



/* how much to allocate before next GC step */
#if !defined(GCSTEPSIZE)
/* ~100 small strings */
#define GCSTEPSIZE	(cast_int(100 * sizeof(TString)))
#endif


/*
** Possible states of the Garbage Collector
*/

/**
 * propagate 和各个 sweep 阶段都是可以每次执行一点，多次执行直到完成的，所以是增量式 gc 增量式过程中依靠 write barrier 来保证一致性
 */

/**
 * 这一步主要就是将所有gray对象变成black,并将其引用到的white对象变成gray,直到没有gray对象存在为止。
 * 在GCSpropagate状态下,barrier会起作用。
 * Lua并不监控所有的引用变化,否则会非常影响效率。
 * 一些我们认为经常会发生变化的地方,比如stack的引用变化,就不用barrier。
 * 从当前 gray 对象出发, 去搜索所有被它们引用的对象,
 * 再从所有被找到的对象出发, 再找所有被它们引用的对象.
 * 这样持续下去直到所有被直接或间接引用到的对象都被找到. 所以叫 propagate (传播)
 * 可以分多次执行，直到 gray 链表处理完，进入 GCSatomic
*/
#define GCSpropagate	0
/**
 * 一次性的处理所有需要回顾一遍的地方, 保证一致性, 然后进入清理阶段
 * 这个阶段是原子性的, 需要一次从头到尾执行一遍, 而不能增量式的每次执行一点
 * 这个阶段感觉主要是进行一些查漏补缺的工作,
 * 把之前 propagate 阶段因为增量式执行引入的问题都解决掉
 * 以一个一致的状态进入接下来的 sweep 阶段
 */
#define GCSatomic	1
/**
 * 清理 allgc 链表, 可以分多次执行, 清理完后进入 GCSswpfinobj
 */
#define GCSswpallgc	2
/**
 * 清理 finobj 链表, 可以分多次执行, 清理完 后进入 GCSswptobefnz
 */
#define GCSswpfinobj	3
/**
 *  清理 tobefnz 链表, 可以分多次执行, 清理完 后进入 GCSswpend
 */
#define GCSswptobefnz	4
/**
 * sweep main thread 然后进入 GCScallfin
 */
#define GCSswpend	5
/**
 *  执行一些 finalizer (__gc) 然后进入 GCSpause, 完成循环
 * setmetatable 时检查 mt 是否有 __gc
 * 如果有则把对象从 allgc 链表转移到 finobj 链表
 * 对象是什么时候被移到 tobefnz 链表的呢?
 * atomic 阶段会调用 separatetobefnz 函数将所有不再存活的对象从 finobj 链表移到 tobefnz 链表等待调用
 * ( 此时会遍历整个 finobj 链表, 因此如果系统中存在太多带有 finalizer 的对象可能在这里会有效率问题 )
 * tobefnz 链表也会被算进 root set, 因此可以保证 __gc 方法调用时所有相关对象都还是存活可以访问的
 */
#define GCScallfin	6
/**
 * GCSpause状态标志着当前没有开始gc。
 * gc一旦开始,第一步要做的就是标识所有的root对象。
 * root对象包括global_State引用的mainthread对象,registry table,全局的metatable和上次gc所产生的还没有进行finalize的垃圾对象。
 * 标识工作就是将white对象设置成gray,是通过函数reallymarkobject进行的。
 * reallymarkobject会根据不同的对象作不同的处理。
 * 对于string对象,本身没有对其它对象的引用,就可以立即设置成black,无需等待后面的遍历。
 * 对于userdata对象,只会引用到一个metatable和env,所以直接mark后也可以立即设置成black。
 * 对于upvalue对象,直接mark引用的对象。
 * 所有root对象会被设置成gray状态,等待下一步的propagate。
 * 第一步完成后,gc状态会切换成GCSpropagate。
*/
#define GCSpause	7


#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again.
*/

#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))


/* Layout for bit use in 'marked' field: */
#define WHITE0BIT	0  /* object is white (type 0) */
#define WHITE1BIT	1  /* object is white (type 1) */
#define BLACKBIT	2  /* object is black */
/**
 * object has been marked for finalization
 * 于标记没有被引用需要回收的udata
 * udata的处理与其他数据类型不同，
 * 由于它是用户传人的数据，它的回收可能会调用用户注册的GC函数，所以统一来处理
*/
#define FINALIZEDBIT	3
/* bit 7 is currently used by tests (luaL_checkmemory) */

#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)


#define iswhite(x)      testbits((x)->marked, WHITEBITS)
#define isblack(x)      testbit((x)->marked, BLACKBIT)
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT)))

#define tofinalize(x)	testbit((x)->marked, FINALIZEDBIT)

#define otherwhite(g)	((g)->currentwhite ^ WHITEBITS)
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->marked)

#define changewhite(x)	((x)->marked ^= WHITEBITS)
#define gray2black(x)	l_setbit((x)->marked, BLACKBIT)

#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)


/*
** Does one step of collection when debt becomes positive. 'pre'/'pos'
** allows some adjustments to be done only when needed. macro
** 'condchangemem' is used only for heavy tests (forcing a full
** GC cycle on every opportunity)
*/
#define luaC_condGC(L,pre,pos) \
	{ if (G(L)->GCdebt > 0) { pre; luaC_step(L); pos;}; \
	  condchangemem(L,pre,pos); }

/* more often than not, 'pre'/'pos' are empty */
/**
 * 它以宏形式定义出来,用于自动的 GC
 * 如果我们审查 lapi.c ldo.c lvm.c 
 * 会发现大部分会导致内存增长的 api 中
 * 都调用了它
 * 保证 gc 可以随内存使用增加而自动进行
 * 使用自动 gc 会有一个问题
 * 它很可能使系统的峰值内存占用远超过实际需求量
 * 原因就在于,收集行为往往发生在调用栈很深的地方
 * 当你的应用程序呈现出某种周期性(大多数包驱动的服务都是这样)
 * 在一个服务周期内,往往会引用众多临时对象
 * 这个时候做 mark 工作
 * 会导致许多临时对象也被 mark 住
 */
#define luaC_checkGC(L)		luaC_condGC(L,(void)0,(void)0)


//把 v 向 p 关联时,当 v 为白色且 p 为黑色时,调用 luaC_barrier_
#define luaC_barrier(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ?  \
	luaC_barrier_(L,obj2gco(p),gcvalue(v)) : cast_void(0))

//将 v 关联到 p 时,调用 luaC_barrierback_  前提条件是 v 为白色,且 p 为黑色
#define luaC_barrierback(L,p,v) (  \
	(iscollectable(v) && isblack(p) && iswhite(gcvalue(v))) ? \
	luaC_barrierback_(L,p) : cast_void(0))

//把 o 向 p 关联时,当 o 为白色且 p 为黑色时,调用 luaC_barrier_
#define luaC_objbarrier(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? \
	luaC_barrier_(L,obj2gco(p),obj2gco(o)) : cast_void(0))

#define luaC_upvalbarrier(L,uv) ( \
	(iscollectable((uv)->v) && !upisopen(uv)) ? \
         luaC_upvalbarrier_(L,uv) : cast_void(0))

LUAI_FUNC void luaC_fix (lua_State *L, GCObject *o);
LUAI_FUNC void luaC_freeallobjects (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, Table *o);
LUAI_FUNC void luaC_upvalbarrier_ (lua_State *L, UpVal *uv);
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt);
LUAI_FUNC void luaC_upvdeccount (lua_State *L, UpVal *uv);


#endif
