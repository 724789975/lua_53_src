/*
 ** $Id: lgc.c,v 2.215.1.2 2017/08/31 16:15:27 roberto Exp $
 ** Garbage Collector
 ** See Copyright Notice in lua.h
 */

#define lgc_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


/*
 ** internal state for collector while inside the atomic phase. The
 ** collector should never be in this state while running regular code.
 */
#define GCSinsideatomic		(GCSpause + 1)

/*
 ** cost of sweeping one element (the size of a small object divided
 ** by some adjust for the sweep speed)
 */
#define GCSWEEPCOST	((sizeof(TString) + 4) / 4)

/* maximum number of elements to sweep in each single step */
#define GCSWEEPMAX	(cast_int((GCSTEPSIZE / GCSWEEPCOST) / 4))

/* cost of calling one finalizer */
#define GCFINALIZECOST	GCSWEEPCOST


/*
 ** macro to adjust 'stepmul': 'stepmul' is actually used like
 ** 'stepmul / STEPMULADJ' (value chosen by tests)
 */
#define STEPMULADJ		200


/*
 ** macro to adjust 'pause': 'pause' is actually used like
 ** 'pause / PAUSEADJ' (value chosen by tests)
 */
#define PAUSEADJ		100


/*
 ** 'makewhite' erases all color bits then sets only the current white
 ** bit
 */
#define maskcolors	(~(bitmask(BLACKBIT) | WHITEBITS))
#define makewhite(g,x)	\
	(x->marked = cast_byte((x->marked & maskcolors) | luaC_white(g)))

#define white2gray(x)	resetbits(x->marked, WHITEBITS)
#define black2gray(x)	resetbit(x->marked, BLACKBIT)


#define valiswhite(x)   (iscollectable(x) && iswhite(gcvalue(x)))

#define checkdeadkey(n)	lua_assert(!ttisdeadkey(gkey(n)) || ttisnil(gval(n)))


//一致性检查
#define checkconsistency(obj)  \
	lua_longassert(!iscollectable(obj) || righttt(obj))


#define markvalue(g,o) { checkconsistency(o); \
	if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); }

#define markobject(g,t)	{ if (iswhite(t)) reallymarkobject(g, obj2gco(t)); }

/*
 ** mark an object that can be NULL (either because it is really optional,
 ** or it was stripped as debug info, or inside an uncompleted structure)
 */
#define markobjectN(g,t)	{ if (t) markobject(g,t); }

static void reallymarkobject (global_State *g, GCObject *o);


/*
 ** {======================================================
 ** Generic functions
 ** =======================================================
 */


/*
 ** one after last element in a hash array
 */
#define gnodelast(h)	gnode(h, cast(size_t, sizenode(h)))


/*
 ** link collectable object 'o' into list pointed by 'p'
 */
#define linkgclist(o,p)	((o)->gclist = (p), (p) = obj2gco(o))


/*
 ** If key is not marked, mark its entry as dead. This allows key to be
 ** collected, but keeps its entry in the table.  A dead node is needed
 ** when Lua looks up for a key (it may be part of a chain) and when
 ** traversing a weak table (key might be removed from the table during
 ** traversal). Other places never manipulate dead keys, because its
 ** associated nil value is enough to signal that the entry is logically
 ** empty.
 */
static void removeentry (Node *n) {
	lua_assert(ttisnil(gval(n)));
	if (valiswhite(gkey(n)))
		setdeadvalue(wgkey(n));  /* unused and unmarked key; remove it */
}


/*
 ** tells whether a key or value can be cleared from a weak
 ** table. Non-collectable objects are never removed from weak
 ** tables. Strings behave as 'values', so are never removed too. for
 ** other objects: if really collected, cannot keep them; for objects
 ** being finalized, keep them in keys, but not in values
 */
static int iscleared (global_State *g, const TValue *o) {
	if (!iscollectable(o)) return 0;
	else if (ttisstring(o)) {
		markobject(g, tsvalue(o));  /* strings are 'values', so are never weak */
		return 0;
	}
	else return iswhite(gcvalue(o));
}

/**
 * barrier that moves collector forward, that is, mark the white object
 * being pointed by a black object. (If in sweep phase, clear the black
 * object to white [sweep it] to avoid other barrier calls for this
 * same object.)
 * luaC_barrier_函数用来实现“向前”的barrier。
 * “向前”的意思就是当一个black对象需要引用一个white对象时,立即mark这个white对象。
 * 这样white对象就变为gray对象,等待下一步的扫描。
 * 这也就是帮助gc向前标识一步。luaC_barrier_函数被用在以下引用变化处:
 * 虚拟机执行过程中或者通过api修改close upvalue对其他对象的引用
 * 通过api设置userdata或table的metatable引用
 * 通过api设置userdata的env table引用
 * 编译构建proto对象过程中proto对象对其他编译产生对象的引用
 */
void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v) {
	global_State *g = G(L);
	lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
	if (keepinvariant(g))  /* must keep invariant? */
	{
		/**
		 * 只要当前的GC没有在扫描标记阶段，就标记这个对象
		 */
		reallymarkobject(g, v);  /* restore invariant */
	}
	else
	{
		/**
		 * sweep phase
		 * 否则将对象标记为白色，等待下一次的GC
		 */
		lua_assert(issweepphase(g));
		makewhite(g, o);  /* mark main obj. as white to avoid other barriers */
	}
}


/**
 * barrier that moves collector backward, that is, mark the black object
 * pointing to a white object as gray again.
 * luaC_barrierback_函数用来实现“向后”的barrier。
 * “向后”的意思就是当一个black对象需要引用一个white对象时,将已经扫描过的black对象再次变为gray对象,等待重新扫描。
 * 这也就是将gc的mark后退一步。luaC_barrierback_目前只用于监控table的key和value对象引用的变化。
 * Table是lua中最主要的数据结构,连全局变量都是被保存在一个table中,所以table的变化是比较频繁的,并且同一个引用可能被反复设置成不同的对象。
 * 对table的引用使用“向前”的barrier,逐个扫描每次引用变化的对象,会造成很多不必要的消耗。
 * 而使用“向后”的barrier就等于将table分成了“未变”和“已变”两种状态。
 * 只要一个table改变了一次,就将其变成gray,等待重新扫描。
 * 被变成gray的table在被重新扫描之前,无论引用再发生多少次变化也都无关紧要了。
 */
void luaC_barrierback_ (lua_State *L, Table *t) {
	global_State *g = G(L);
	lua_assert(isblack(t) && !isdead(g, t));
	black2gray(t);  /* make table gray (again) */
	/**
	 * 需要进行barrierback操作的对象，
	 * 最后并没有如新建对象那样加入gray链表中，
	 * 而是加入grayagain列表中，
	 * 避免一个对象频繁地进行“被回退－扫描－回退－扫描”过程。
	 * 既然需要重新扫描，那么一次性地放在grayagain链表中就可以了
	 */
	linkgclist(t, g->grayagain);
}


/*
 ** barrier for assignments to closed upvalues. Because upvalues are
 ** shared among closures, it is impossible to know the color of all
 ** closures pointing to it. So, we assume that the object being assigned
 ** must be marked.
 */
void luaC_upvalbarrier_ (lua_State *L, UpVal *uv) {
	global_State *g = G(L);
	GCObject *o = gcvalue(uv->v);
	lua_assert(!upisopen(uv));  /* ensured by macro luaC_upvalbarrier */
	if (keepinvariant(g))
		markobject(g, o);
}


void luaC_fix (lua_State *L, GCObject *o) {
	global_State *g = G(L);
	lua_assert(g->allgc == o);  /* object must be 1st in 'allgc' list! */
	white2gray(o);  /* they will be gray forever */
	g->allgc = o->next;  /* remove object from 'allgc' list */
	o->next = g->fixedgc;  /* link it to 'fixedgc' list */
	g->fixedgc = o;
}


/**
 * create a new collectable object (with given type and size) and link
 * it to 'allgc' list.
 */
GCObject *luaC_newobj (lua_State *L, int tt, size_t sz) {
	global_State *g = G(L);
	GCObject *o = cast(GCObject *, luaM_newobject(L, novariant(tt), sz));
	o->marked = luaC_white(g);
	o->tt = tt;
	o->next = g->allgc;
	g->allgc = o;
	return o;
}

/* }====================================================== */



/*
 ** {======================================================
 ** Mark functions
 ** =======================================================
 */


/*
 ** mark an object. Userdata, strings, and closed upvalues are visited
 ** and turned black here. Other objects are marked gray and added
 ** to appropriate list to be visited (and turned black) later. (Open
 ** upvalues are already linked in 'headuv' list.)
 */
/**
 * 它按 GCObject 的实际类型来 mark 它
 * reallymarkobject 的时间复杂度是 O(1) 的
 * 它不会递归标记相关对象,虽然大多数 GCObject 都关联有其它对象
 * 保证 O(1) 时间使得标记过程可以均匀分摊在逐个短小的时间片内,不至于停止世界太久
 * 这里就需要用到三色标记法
 * reallymarkobject 进入时,先把对象设置为灰色(通过 white2gray 这个宏)
 * 然后再根据具体类型,当一个对象的所有关联对象都被标记后,再从灰色转为黑色
 * 因为 TSTRING 一定没有关联对象,而且所有的字符串都是统一独立处理的
 * 这里可以做一个小优化,不需要设置为黑色,只要不是白色就可以清理。所以此处不必染黑???(53.的代码是有染黑的 代码如下)
 * 但 TUSERDATA 则不同,它是跟其它对象一起处理的。标记 userdata 就需要调用 gray2black 了
 * 另外,还需要标记 userdata 的元表和环境表
 * TUPVAL 是一个特殊的东西
 * 在 lua 编程,以及写 C 代码和 lua 交互时,都看不到这种类型
 * 它用来解决多个 closure 共享同一个 upvalue 的情况
 * 实际上是对一个 upvalue 的引用
 * 问什么 TUPVAL 会有 open 和 closed 两种状态？应该这样理解
 * 当一个 lua 函数本执行的时候
 * 和 C 语言不一样,它不仅可以看到当前层次上的 local 变量
 * 还可以看到上面所有层次的 local 变量
 * 这个可见性是由 lua 解析器解析你的 lua 代码时定位的(换句话说,就是在“编译”期决定的)
 * 那些不属于你的函数当前层次上的 local 变量,就称之为 upvalue 
 * upvalue 这个概念是由 parser 引入的
 * 在 Lua 中,任何一个 function 其实都是由 proto 和运行时绑定的 upvalue 构成的
 * proto 将如何绑定 upvalue 是在 parser 生成的 bytecode 里描述清楚了的
 */
static void reallymarkobject (global_State *g, GCObject *o) {
reentry:
	white2gray(o);
	switch (o->tt)
	{
		case LUA_TSHRSTR:
			{
				/**
				 * 对于字符串类型的数据，
				 * 由于这种类型没有引用其他数据，
				 * 所以直接标记为黑色
				 */
				gray2black(o);
				g->GCmemtrav += sizelstring(gco2ts(o)->shrlen);
				break;
			}
		case LUA_TLNGSTR:
			{
				/**
				 * 对于字符串类型的数据，
				 * 由于这种类型没有引用其他数据，
				 * 所以直接标记为黑色
				 */
				gray2black(o);
				g->GCmemtrav += sizelstring(gco2ts(o)->u.lnglen);
				break;
			}
		case LUA_TUSERDATA:
			{
				/**
				 * 对于udata类型的数据，
				 * 因为这种类型永远不会引用其他数据，
				 * 所以这里一步到位，直接将其标记为黑色。
				 * 另外，对于这种类型，还需要标记对应的metatable和env表。
				 */
				TValue uvalue;
				markobjectN(g, gco2u(o)->metatable);	/* mark its metatable */
				gray2black(o);
				g->GCmemtrav += sizeudata(gco2u(o));
				getuservalue(g->mainthread, gco2u(o), &uvalue);
				if (valiswhite(&uvalue)) {	/* markvalue(g, &uvalue); */
					o = gcvalue(&uvalue);
					goto reentry;
				}
				break;
			}
		case LUA_TLCL:
			{
				linkgclist(gco2lcl(o), g->gray);
				break;
			}
		case LUA_TCCL:
			{
				linkgclist(gco2ccl(o), g->gray);
				break;
			}
		case LUA_TTABLE:
			{
				linkgclist(gco2t(o), g->gray);
				break;
			}
		case LUA_TTHREAD:
			{
				linkgclist(gco2th(o), g->gray);
				break;
			}
		case LUA_TPROTO:
			{
				linkgclist(gco2p(o), g->gray);
				break;
			}
		default: lua_assert(0); break;
	}
}


/*
 ** mark metamethods for basic types
 */
static void markmt (global_State *g) {
	int i;
	for (i=0; i < LUA_NUMTAGS; i++)
		markobjectN(g, g->mt[i]);
}


/**
 * mark all objects in list of being-finalized
 * 标记上次GC循环中剩余的finalize中的对象，并将其加入对应的辅助标记链中
 */
static void markbeingfnz (global_State *g) {
	GCObject *o;
	for (o = g->tobefnz; o != NULL; o = o->next)
		markobject(g, o);
}


/*
 ** Mark all values stored in marked open upvalues from non-marked threads.
 ** (Values from marked threads were already marked when traversing the
 ** thread.) Remove from the list threads that no longer have upvalues and
 ** not-marked threads.
 */
static void remarkupvals (global_State *g) {
	lua_State *thread;
	lua_State **p = &g->twups;
	while ((thread = *p) != NULL) {
		lua_assert(!isblack(thread));  /* threads are never black */
		if (isgray(thread) && thread->openupval != NULL)
			p = &thread->twups;  /* keep marked thread with upvalues in the list */
		else {  /* thread is not marked or without upvalues */
			UpVal *uv;
			*p = thread->twups;  /* remove thread from the list */
			thread->twups = thread;  /* mark that it is out of list */
			for (uv = thread->openupval; uv != NULL; uv = uv->u.open.next) {
				if (uv->u.open.touched) {
					markvalue(g, uv->v);  /* remark upvalue's value */
					uv->u.open.touched = 0;
				}
			}
		}
	}
}


/**
 * mark root set and reset all gray lists, to start a new collection
 */
static void restartcollection (global_State *g) {
	g->gray = g->grayagain = NULL;
	g->weak = g->allweak = g->ephemeron = NULL;
	/**
	 * markobject和markvalue函数都用于标记对象的颜色为灰色，
	 * `不同的是前者是针对object而后者是针对TValue，它们最终都会调用reallymarkobject函数
	 */
	markobject(g, g->mainthread);
	markvalue(g, &g->l_registry);
	markmt(g);
	markbeingfnz(g);  /* mark any finalizing object left from previous cycle */
}

/* }====================================================== */


/*
 ** {======================================================
 ** Traverse functions
 ** =======================================================
 */

/*
 ** Traverse a table with weak values and link it to proper list. During
 ** propagate phase, keep it in 'grayagain' list, to be revisited in the
 ** atomic phase. In the atomic phase, if table has any white value,
 ** put it in 'weak' list, to be cleared.
 */
static void traverseweakvalue (global_State *g, Table *h) {
	Node *n, *limit = gnodelast(h);
	/* if there is array part, assume it may have white values (it is not
	   worth traversing it now just to check) */
	int hasclears = (h->sizearray > 0);
	for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part */
		checkdeadkey(n);
		if (ttisnil(gval(n)))  /* entry is empty? */
			removeentry(n);  /* remove it */
		else {
			lua_assert(!ttisnil(gkey(n)));
			markvalue(g, gkey(n));  /* mark key */
			if (!hasclears && iscleared(g, gval(n)))  /* is there a white value? */
				hasclears = 1;  /* table will have to be cleared */
		}
	}
	if (g->gcstate == GCSpropagate)
		linkgclist(h, g->grayagain);  /* must retraverse it in atomic phase */
	else if (hasclears)
		linkgclist(h, g->weak);  /* has to be cleared later */
}


/*
 ** Traverse an ephemeron table and link it to proper list. Returns true
 ** iff any object was marked during this traversal (which implies that
 ** convergence has to continue). During propagation phase, keep table
 ** in 'grayagain' list, to be visited again in the atomic phase. In
 ** the atomic phase, if table has any white->white entry, it has to
 ** be revisited during ephemeron convergence (as that key may turn
 ** black). Otherwise, if it has any white key, table has to be cleared
 ** (in the atomic phase).
 */
static int traverseephemeron (global_State *g, Table *h) {
	int marked = 0;  /* true if an object is marked in this traversal */
	int hasclears = 0;  /* true if table has white keys */
	int hasww = 0;  /* true if table has entry "white-key -> white-value" */
	Node *n, *limit = gnodelast(h);
	unsigned int i;
	/* traverse array part */
	for (i = 0; i < h->sizearray; i++) {
		if (valiswhite(&h->array[i])) {
			marked = 1;
			reallymarkobject(g, gcvalue(&h->array[i]));
		}
	}
	/* traverse hash part */
	for (n = gnode(h, 0); n < limit; n++) {
		checkdeadkey(n);
		if (ttisnil(gval(n)))  /* entry is empty? */
			removeentry(n);  /* remove it */
		else if (iscleared(g, gkey(n))) {  /* key is not marked (yet)? */
			hasclears = 1;  /* table must be cleared */
			if (valiswhite(gval(n)))  /* value not marked yet? */
				hasww = 1;  /* white-white entry */
		}
		else if (valiswhite(gval(n))) {  /* value not marked yet? */
			marked = 1;
			reallymarkobject(g, gcvalue(gval(n)));  /* mark it now */
		}
	}
	/* link table into proper list */
	if (g->gcstate == GCSpropagate)
		linkgclist(h, g->grayagain);  /* must retraverse it in atomic phase */
	else if (hasww)  /* table has white->white entries? */
		linkgclist(h, g->ephemeron);  /* have to propagate again */
	else if (hasclears)  /* table has white keys? */
		linkgclist(h, g->allweak);  /* may have to clean white keys */
	return marked;
}


static void traversestrongtable (global_State *g, Table *h) {
	Node *n, *limit = gnodelast(h);
	unsigned int i;
	for (i = 0; i < h->sizearray; i++)  /* traverse array part */
		markvalue(g, &h->array[i]);
	for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part */
		checkdeadkey(n);
		if (ttisnil(gval(n)))  /* entry is empty? */
			removeentry(n);  /* remove it */
		else {
			lua_assert(!ttisnil(gkey(n)));
			markvalue(g, gkey(n));  /* mark key */
			markvalue(g, gval(n));  /* mark value */
		}
	}
}


/**
 * 在traversetable函数中，
 * 如果扫描到该表是弱表，那么将会把该对象加入weak链表中，
 * 这个链表将在扫描阶段的最后一步进行一次不能中断的处理。
 * 同时，如果该表是弱表，那么将该对象回退到灰色状态，重新进行扫描。
 * 在不是弱表的情况下，将遍历标记表的散列部分及数组部分的所有元素
 */
static lu_mem traversetable (global_State *g, Table *h) {
	const char *weakkey, *weakvalue;
	const TValue *mode = gfasttm(g, h->metatable, TM_MODE);
	markobjectN(g, h->metatable);
	if (mode && ttisstring(mode) &&  /* is there a weak mode? */
			((weakkey = strchr(svalue(mode), 'k')),
			 (weakvalue = strchr(svalue(mode), 'v')),
			 (weakkey || weakvalue)
			)
	   )
	{
		/**
		 * is really weak?
		 * 到该表是弱表，那么将会把该对象加入weak链表中，
		 * 这个链表将在扫描阶段的最后一步进行一次不能中断的处理。
		 * 同时，如果该表是弱表，那么将该对象回退到灰色状态，重新进行扫描
		 */
		black2gray(h);  /* keep table gray */
		if (!weakkey)  /* strong keys? */
			traverseweakvalue(g, h);
		else if (!weakvalue)  /* strong values? */
			traverseephemeron(g, h);
		else  /* all weak */
			linkgclist(h, g->allweak);  /* nothing to traverse now */
	}
	else
	{
		/**
		 * not weak
		 * 在不是弱表的情况下，将遍历标记表的散列部分及数组部分的所有元素。
		 */
		traversestrongtable(g, h);
	}
	return sizeof(Table) + sizeof(TValue) * h->sizearray +
		sizeof(Node) * cast(size_t, allocsizenode(h));
}


/**
 * Traverse a prototype. (While a prototype is being build, its
 * arrays can be larger than needed; the extra slots are filled with
 * NULL, so the use of 'markobjectN')
 * 函数标记一个Proto数据中的文件 名、字符串、upvalue、局部变量等所有被引用的对象。 
 */
static int traverseproto (global_State *g, Proto *f) {
	int i;
	if (f->cache && iswhite(f->cache))
		f->cache = NULL;  /* allow cache to be collected */
	markobjectN(g, f->source);
	for (i = 0; i < f->sizek; i++)  /* mark literals */
		markvalue(g, &f->k[i]);
	for (i = 0; i < f->sizeupvalues; i++)  /* mark upvalue names */
		markobjectN(g, f->upvalues[i].name);
	for (i = 0; i < f->sizep; i++)  /* mark nested protos */
		markobjectN(g, f->p[i]);
	for (i = 0; i < f->sizelocvars; i++)  /* mark local-variable names */
		markobjectN(g, f->locvars[i].varname);
	return sizeof(Proto) + sizeof(Instruction) * f->sizecode +
		sizeof(Proto *) * f->sizep +
		sizeof(TValue) * f->sizek +
		sizeof(int) * f->sizelineinfo +
		sizeof(LocVar) * f->sizelocvars +
		sizeof(Upvaldesc) * f->sizeupvalues;
}


/**
 * 数主要是对C函数中的所有UpValue进行标记
 */
static lu_mem traverseCclosure (global_State *g, CClosure *cl) {
	int i;
	for (i = 0; i < cl->nupvalues; i++)  /* mark its upvalues */
		markvalue(g, &cl->upvalue[i]);
	return sizeCclosure(cl->nupvalues);
}

/**
 * open upvalues point to values in a thread, so those values should
 * be marked when the thread is traversed except in the atomic phase
 * (because then the value cannot be changed by the thread and the
 * thread may not be traversed again)
 * 数主要是对Lua函数中的所有UpValue进行标记
 */
static lu_mem traverseLclosure (global_State *g, LClosure *cl) {
	int i;
	markobjectN(g, cl->p);  /* mark its prototype */
	for (i = 0; i < cl->nupvalues; i++) {  /* mark its upvalues */
		UpVal *uv = cl->upvals[i];
		if (uv != NULL) {
			if (upisopen(uv) && g->gcstate != GCSinsideatomic)
				uv->u.open.touched = 1;  /* can be marked in 'remarkupvals' */
			else
				markvalue(g, uv->v);
		}
	}
	return sizeLclosure(cl->nupvalues);
}

/**
*/
static lu_mem traversethread (global_State *g, lua_State *th) {
	StkId o = th->stack;
	if (o == NULL)
		return 1;  /* stack not completely built yet */
	lua_assert(g->gcstate == GCSinsideatomic ||
			th->openupval == NULL || isintwups(th));
	for (; o < th->top; o++)  /* mark live elements in the stack */
		markvalue(g, o);
	if (g->gcstate == GCSinsideatomic) {  /* final traversal? */
		StkId lim = th->stack + th->stacksize;  /* real end of stack */
		for (; o < lim; o++)  /* clear not-marked stack slice */
			setnilvalue(o);
		/* 'remarkupvals' may have removed thread from 'twups' list */
		if (!isintwups(th) && th->openupval != NULL) {
			th->twups = g->twups;  /* link it back to the list */
			g->twups = th;
		}
	}
	else if (g->gckind != KGC_EMERGENCY)
		luaD_shrinkstack(th); /* do not change stack in emergency cycle */
	return (sizeof(lua_State) + sizeof(TValue) * th->stacksize +
			sizeof(CallInfo) * th->nci);
}


/**
 * traverse one gray object, turning it to black (except for threads,
 * which are always gray).
 * 里将对象从灰色标记成黑色，表示这个对象及其所引用的对象都已经标记过
 * 会根据不同的类型调用对应的traverse*函数进行标记（会递归调用）
 */
static void propagatemark (global_State *g) {
	lu_mem size;
	GCObject *o = g->gray;
	lua_assert(isgray(o));
	gray2black(o);
	switch (o->tt)
	{
		case LUA_TTABLE:
			{
				Table *h = gco2t(o);
				g->gray = h->gclist;  /* remove from 'gray' list */
				size = traversetable(g, h);
				break;
			}
		case LUA_TLCL:
			{
				LClosure *cl = gco2lcl(o);
				g->gray = cl->gclist;  /* remove from 'gray' list */
				size = traverseLclosure(g, cl);
				break;
			}
		case LUA_TCCL:
			{
				CClosure *cl = gco2ccl(o);
				g->gray = cl->gclist;  /* remove from 'gray' list */
				size = traverseCclosure(g, cl);
				break;
			}
		case LUA_TTHREAD:
			{
				lua_State *th = gco2th(o);
				g->gray = th->gclist;  /* remove from 'gray' list */
				linkgclist(th, g->grayagain);  /* insert into 'grayagain' list */
				black2gray(o);
				size = traversethread(g, th);
				break;
			}
		case LUA_TPROTO:
			{
				Proto *p = gco2p(o);
				g->gray = p->gclist;  /* remove from 'gray' list */
				size = traverseproto(g, p);
				break;
			}
		default: lua_assert(0); return;
	}
	g->GCmemtrav += size;
}


static void propagateall (global_State *g) {
	while (g->gray) propagatemark(g);
}


/**
*/
static void convergeephemerons (global_State *g) {
	int changed;	// 关键值，指示对 g->ephemeron 的一次遍历中是否有对象被重新mark
	do {
		GCObject *w;
		GCObject *next = g->ephemeron;  /* get ephemeron list */
		g->ephemeron = NULL;  /* tables may return to this list when traversed */
		changed = 0;
		while ((w = next) != NULL) {
			next = gco2t(w)->gclist;
			if (traverseephemeron(g, gco2t(w))) {  /* traverse marked some value? */
				propagateall(g);  /* propagate changes */
				changed = 1;  /* will have to revisit all ephemeron tables */
			}
		}
	} while (changed);	// 无对象可 mark 时，退出
}

/* }====================================================== */


/*
 ** {======================================================
 ** Sweep Functions
 ** =======================================================
 */


/*
 ** clear entries with unmarked keys from all weaktables in list 'l' up
 ** to element 'f'
 */
static void clearkeys (global_State *g, GCObject *l, GCObject *f) {
	for (; l != f; l = gco2t(l)->gclist) {
		Table *h = gco2t(l);
		Node *n, *limit = gnodelast(h);
		for (n = gnode(h, 0); n < limit; n++) {
			if (!ttisnil(gval(n)) && (iscleared(g, gkey(n)))) {
				setnilvalue(gval(n));  /* remove value ... */
			}
			if (ttisnil(gval(n)))  /* is entry empty? */
				removeentry(n);  /* remove entry from table */
		}
	}
}


/*
 ** clear entries with unmarked values from all weaktables in list 'l' up
 ** to element 'f'
 */
static void clearvalues (global_State *g, GCObject *l, GCObject *f) {
	for (; l != f; l = gco2t(l)->gclist) {
		Table *h = gco2t(l);
		Node *n, *limit = gnodelast(h);
		unsigned int i;
		for (i = 0; i < h->sizearray; i++) {
			TValue *o = &h->array[i];
			if (iscleared(g, o))  /* value was collected? */
				setnilvalue(o);  /* remove value */
		}
		for (n = gnode(h, 0); n < limit; n++) {
			if (!ttisnil(gval(n)) && iscleared(g, gval(n))) {
				setnilvalue(gval(n));  /* remove value ... */
				removeentry(n);  /* and remove entry from table */
			}
		}
	}
}


void luaC_upvdeccount (lua_State *L, UpVal *uv) {
	lua_assert(uv->refcount > 0);
	uv->refcount--;
	if (uv->refcount == 0 && !upisopen(uv))
		luaM_free(L, uv);
}


static void freeLclosure (lua_State *L, LClosure *cl) {
	int i;
	for (i = 0; i < cl->nupvalues; i++) {
		UpVal *uv = cl->upvals[i];
		if (uv)
			luaC_upvdeccount(L, uv);
	}
	luaM_freemem(L, cl, sizeLclosure(cl->nupvalues));
}


static void freeobj (lua_State *L, GCObject *o) {
	switch (o->tt)
	{
		case LUA_TPROTO: luaF_freeproto(L, gco2p(o)); break;
		case LUA_TLCL:
						 {
							 freeLclosure(L, gco2lcl(o));
							 break;
						 }
		case LUA_TCCL:
						 {
							 luaM_freemem(L, o, sizeCclosure(gco2ccl(o)->nupvalues));
							 break;
						 }
		case LUA_TTABLE: luaH_free(L, gco2t(o)); break;
		case LUA_TTHREAD: luaE_freethread(L, gco2th(o)); break;
		case LUA_TUSERDATA: luaM_freemem(L, o, sizeudata(gco2u(o))); break;
		case LUA_TSHRSTR:
							luaS_remove(L, gco2ts(o));  /* remove it from hash table */
							luaM_freemem(L, o, sizelstring(gco2ts(o)->shrlen));
							break;
		case LUA_TLNGSTR:
							{
								luaM_freemem(L, o, sizelstring(gco2ts(o)->u.lnglen));
								break;
							}
		default: lua_assert(0);
	}
}


#define sweepwholelist(L,p)	sweeplist(L,p,MAX_LUMEM)
static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count);


/**
 * sweep at most 'count' elements from a list of GCObjects erasing dead
 * objects, where a dead object is one marked with the old (non current)
 * white; change all non-dead objects back to white, preparing for next
 * collection cycle. Return where to continue the traversal or NULL if
 * list is finished.
 */
static GCObject **sweeplist (lua_State *L, GCObject **p, lu_mem count) {
	global_State *g = G(L);
	int ow = otherwhite(g);		//本次GC操作不可以被回收的白色类型。
	int white = luaC_white(g);  /* current white */

	/**
	 * 依次遍历链表中的数据，判断每个对象的白色是否满足被回收的颜色条件
	 */
	while (*p != NULL && count-- > 0) {
		GCObject *curr = *p;
		int marked = curr->marked;
		if (isdeadm(ow, marked)) {  /* is 'curr' dead? */
			*p = curr->next;  /* remove 'curr' from list */
			freeobj(L, curr);  /* erase 'curr' */
		}
		else {  /* change mark to 'white' */
			curr->marked = cast_byte((marked & maskcolors) | white);
			p = &curr->next;  /* go to next element */
		}
	}
	return (*p == NULL) ? NULL : p;
}


/*
 ** sweep a list until a live object (or end of list)
 */
static GCObject **sweeptolive (lua_State *L, GCObject **p) {
	GCObject **old = p;
	do {
		p = sweeplist(L, p, 1);
	} while (p == old);
	return p;
}

/* }====================================================== */


/*
 ** {======================================================
 ** Finalization
 ** =======================================================
 */

/*
 ** If possible, shrink string table
 */
static void checkSizes (lua_State *L, global_State *g) {
	if (g->gckind != KGC_EMERGENCY) {
		l_mem olddebt = g->GCdebt;
		if (g->strt.nuse < g->strt.size / 4)  /* string table too big? */
			luaS_resize(L, g->strt.size / 2);  /* shrink it a little */
		g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
	}
}


static GCObject *udata2finalize (global_State *g) {
	GCObject *o = g->tobefnz;  /* get first element */
	lua_assert(tofinalize(o));
	g->tobefnz = o->next;  /* remove it from 'tobefnz' list */
	o->next = g->allgc;  /* return it to 'allgc' list */
	g->allgc = o;
	resetbit(o->marked, FINALIZEDBIT);  /* object is "normal" again */
	if (issweepphase(g))
		makewhite(g, o);  /* "sweep" object */
	return o;
}


static void dothecall (lua_State *L, void *ud) {
	UNUSED(ud);
	luaD_callnoyield(L, L->top - 2, 0);
}


/**
 * 循环遍历tmudata链表中的对象，针对每个对象调用fasttm函数，其中会使用GC元方法来进行对象的回收
 */
static void GCTM (lua_State *L, int propagateerrors) {
	global_State *g = G(L);
	const TValue *tm;
	TValue v;
	setgcovalue(L, &v, udata2finalize(g));
	tm = luaT_gettmbyobj(L, &v, TM_GC);
	if (tm != NULL && ttisfunction(tm)) {  /* is there a finalizer? */
		int status;
		lu_byte oldah = L->allowhook;
		int running  = g->gcrunning;
		L->allowhook = 0;  /* stop debug hooks during GC metamethod */
		g->gcrunning = 0;  /* avoid GC steps */
		setobj2s(L, L->top, tm);  /* push finalizer... */
		setobj2s(L, L->top + 1, &v);  /* ... and its argument */
		L->top += 2;  /* and (next line) call the finalizer */
		L->ci->callstatus |= CIST_FIN;  /* will run a finalizer */
		status = luaD_pcall(L, dothecall, NULL, savestack(L, L->top - 2), 0);
		L->ci->callstatus &= ~CIST_FIN;  /* not running a finalizer anymore */
		L->allowhook = oldah;  /* restore hooks */
		g->gcrunning = running;  /* restore state */
		if (status != LUA_OK && propagateerrors) {  /* error while running __gc? */
			if (status == LUA_ERRRUN) {  /* is there an error object? */
				const char *msg = (ttisstring(L->top - 1))
					? svalue(L->top - 1)
					: "no message";
				luaO_pushfstring(L, "error in __gc metamethod (%s)", msg);
				status = LUA_ERRGCMM;  /* error in __gc metamethod */
			}
			luaD_throw(L, status);  /* re-throw error */
		}
	}
}


/*
 ** call a few (up to 'g->gcfinnum') finalizers
 */
static int runafewfinalizers (lua_State *L) {
	global_State *g = G(L);
	unsigned int i;
	lua_assert(!g->tobefnz || g->gcfinnum > 0);
	for (i = 0; g->tobefnz && i < g->gcfinnum; i++)
		GCTM(L, 1);  /* call one finalizer */
	g->gcfinnum = (!g->tobefnz) ? 0  /* nothing more to finalize? */
		: g->gcfinnum * 2;  /* else call a few more next time */
	return i;
}


/*
 ** call all pending finalizers
 */
static void callallpendingfinalizers (lua_State *L) {
	global_State *g = G(L);
	while (g->tobefnz)
		GCTM(L, 0);
}


/*
 ** find last 'next' field in list 'p' list (to add elements in its end)
 */
static GCObject **findlast (GCObject **p) {
	while (*p != NULL)
		p = &(*p)->next;
	return p;
}


/**
 * move all unreachable objects (or 'all' objects) that need
 * finalization from list 'finobj' to list 'tobefnz' (to be finalized)
 * 将所有不再存活的对象从finobj链表移到tobefnz链表等待调用
 * 此时会遍历整个 finobj 链表, 因此如果系统中存在太多带有 finalizer 的对象可能在这里会有效率问题
 */
static void separatetobefnz (global_State *g, int all) {
	GCObject *curr;
	GCObject **p = &g->finobj;
	GCObject **lastnext = findlast(&g->tobefnz);
	while ((curr = *p) != NULL) {  /* traverse all finalizable objects */
		lua_assert(tofinalize(curr));
		if (!(iswhite(curr) || all))  /* not being collected? */
			p = &curr->next;  /* don't bother with it */
		else {
			*p = curr->next;  /* remove 'curr' from 'finobj' list */
			curr->next = *lastnext;  /* link at the end of 'tobefnz' list */
			*lastnext = curr;
			lastnext = &curr->next;
		}
	}
}


/*
 ** if object 'o' has a finalizer, remove it from 'allgc' list (must
 ** search the list to find it) and link it in 'finobj' list.
 */
void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt) {
	global_State *g = G(L);
	if (tofinalize(o) ||                 /* obj. is already marked... */
			gfasttm(g, mt, TM_GC) == NULL)   /* or has no finalizer? */
		return;  /* nothing to be done */
	else {  /* move 'o' to 'finobj' list */
		GCObject **p;
		if (issweepphase(g)) {
			makewhite(g, o);  /* "sweep" object 'o' */
			if (g->sweepgc == &o->next)  /* should not remove 'sweepgc' object */
				g->sweepgc = sweeptolive(L, g->sweepgc);  /* change 'sweepgc' */
		}
		/* search for pointer pointing to 'o' */
		for (p = &g->allgc; *p != o; p = &(*p)->next) { /* empty */ }
		*p = o->next;  /* remove 'o' from 'allgc' list */
		o->next = g->finobj;  /* link it in 'finobj' list */
		g->finobj = o;
		l_setbit(o->marked, FINALIZEDBIT);  /* mark it as such */
	}
}

/* }====================================================== */



/*
 ** {======================================================
 ** GC control
 ** =======================================================
 */


/*
 ** Set a reasonable "time" to wait before starting a new GC cycle; cycle
 ** will start when memory use hits threshold. (Division by 'estimate'
 ** should be OK: it cannot be zero (because Lua cannot even start with
 ** less than PAUSEADJ bytes).
 */
static void setpause (global_State *g) {
	l_mem threshold, debt;
	l_mem estimate = g->GCestimate / PAUSEADJ;  /* adjust 'estimate' */
	lua_assert(estimate > 0);
	threshold = (g->gcpause < MAX_LMEM / estimate)  /* overflow? */
		? estimate * g->gcpause  /* no overflow */
		: MAX_LMEM;  /* overflow; truncate to maximum */
	debt = gettotalbytes(g) - threshold;
	luaE_setdebt(g, debt);
}


/*
 ** Enter first sweep phase.
 ** The call to 'sweeplist' tries to make pointer point to an object
 ** inside the list (instead of to the header), so that the real sweep do
 ** not need to skip objects created between "now" and the start of the
 ** real sweep.
 */
static void entersweep (lua_State *L) {
	global_State *g = G(L);
	g->gcstate = GCSswpallgc;
	lua_assert(g->sweepgc == NULL);
	g->sweepgc = sweeplist(L, &g->allgc, 1);
}

/**
 * 释放全部对象
 */
void luaC_freeallobjects (lua_State *L) {
	global_State *g = G(L);
	separatetobefnz(g, 1);  /* separate all objects with finalizers */
	lua_assert(g->finobj == NULL);
	callallpendingfinalizers(L);
	lua_assert(g->tobefnz == NULL);
	g->currentwhite = WHITEBITS; /* this "white" makes all objects look dead */
	g->gckind = KGC_NORMAL;
	sweepwholelist(L, &g->finobj);
	sweepwholelist(L, &g->allgc);
	sweepwholelist(L, &g->fixedgc);  /* collect fixed objects */
	lua_assert(g->strt.nuse == 0);
}


static l_mem atomic (lua_State *L) {
	global_State *g = G(L);
	l_mem work;
	GCObject *origweak, *origall;
	GCObject *grayagain = g->grayagain;  /* save original list */
	lua_assert(g->ephemeron == NULL && g->weak == NULL);
	lua_assert(!iswhite(g->mainthread));
	g->gcstate = GCSinsideatomic;
	g->GCmemtrav = 0;  /* start counting work */
	markobject(g, L);  /* mark running thread */
	/* registry and global metatables may be changed by API */
	markvalue(g, &g->l_registry);
	markmt(g);  /* mark global metatables */
	/**
	 * remark occasional upvalues of (maybe) dead threads
	 * 用remarkupvals函数去标记open状态的UpValue，
	 * 这一步完毕之后， gray链表又会有新的对象，
	 * 于是需要调用propagateall再次将gray链表中的对象标记一下
	 */
	remarkupvals(g);
	propagateall(g);  /* propagate changes */

	/**
	 * 修改gray链表指针指向grayagain指针，同样是调用propagateall函数进行遍历扫描操作
	 */
	work = g->GCmemtrav;  /* stop counting (do not recount 'grayagain') */
	g->gray = grayagain;
	propagateall(g);  /* traverse 'grayagain' list */

	g->GCmemtrav = 0;  /* restart counting */
	/**
	 * 键是否可达已最终确定，mark 掉其可达键所关联的值
	 */
	convergeephemerons(g);
	/* at this point, all strongly accessible objects are marked. */
	/* Clear values from weak tables, before checking finalizers */
	clearvalues(g, g->weak, NULL);
	clearvalues(g, g->allweak, NULL);
	origweak = g->weak; origall = g->allweak;

	work += g->GCmemtrav;  /* stop counting (objects being finalized) */
	/**
	 * separate objects to be finalized
	 * 将所有不再存活的对象从finobj链表移到tobefnz链表等待调用
	 * 此时会遍历整个 finobj 链表, 因此如果系统中存在太多带有 finalizer 的对象可能在这里会有效率问题
	 */
	separatetobefnz(g, 0);

	g->gcfinnum = 1;  /* there may be objects to be finalized */
	/**
	 * 标记上次GC循环中剩余的finalize中的对象，并将其加入对应的辅助标记链中
	 */
	markbeingfnz(g);  /* mark objects that will be finalized */
	propagateall(g);  /* remark, to propagate 'resurrection' */

	g->GCmemtrav = 0;  /* restart counting */
	/**
	 * 复活需 finalizer 的对象及这些对象所关联的对象后，重新mark ephemeron
	 */
	convergeephemerons(g);
	/* at this point, all resurrected objects are marked. */
	/* remove dead objects from weak tables */
	clearkeys(g, g->ephemeron, NULL);  /* clear keys from all ephemeron tables */
	clearkeys(g, g->allweak, NULL);  /* clear keys from all 'allweak' tables */
	/* clear values from resurrected weak tables */
	clearvalues(g, g->weak, origweak);
	clearvalues(g, g->allweak, origall);
	luaS_clearcache(g);

	/**
	 * flip current white
	 * 将当前白色类型切换到了下一次GC操作的白色类型。 
	 */
	g->currentwhite = cast_byte(otherwhite(g));

	work += g->GCmemtrav;  /* complete counting */
	return work;  /* estimate of memory marked by 'atomic' */
}


static lu_mem sweepstep (lua_State *L, global_State *g,
		int nextstate, GCObject **nextlist) {
	if (g->sweepgc) {
		l_mem olddebt = g->GCdebt;
		g->sweepgc = sweeplist(L, g->sweepgc, GCSWEEPMAX);
		g->GCestimate += g->GCdebt - olddebt;  /* update estimate */
		if (g->sweepgc)  /* is there still something to sweep? */
			return (GCSWEEPMAX * GCSWEEPCOST);
	}
	/* else enter next state */
	g->gcstate = nextstate;
	g->sweepgc = nextlist;
	return 0;
}


static lu_mem singlestep (lua_State *L) {
	global_State *g = G(L);
	switch (g->gcstate) {
		case GCSpause:		// 一段GC循环的开始
			{
				g->GCmemtrav = g->strt.size * sizeof(GCObject*);
				restartcollection(g);		//标记为灰色
				g->gcstate = GCSpropagate;
				return g->GCmemtrav;
			}
		case GCSpropagate:	// 传播阶段
			{
				g->GCmemtrav = 0;
				lua_assert(g->gray);
				propagatemark(g);		// 转换灰成黑（除了线程，一直是灰）
				if (g->gray == NULL)	// 如果没有灰对象了，就执行下一个阶段 /* no more gray objects? */
					g->gcstate = GCSatomic;  /* finish propagate phase */
				return g->GCmemtrav;  /* memory traversed in this step */
			}
		case GCSatomic:
			{
				lu_mem work;
				propagateall(g);  /* make sure gray list is empty */
				work = atomic(L);  /* work is what was traversed by 'atomic' */
				entersweep(L);		// 进入回收阶段
				g->GCestimate = gettotalbytes(g);  /* first estimate */;
				return work;
			}
		case GCSswpallgc:		//回收阶段
			{  /* sweep "regular" objects */
				return sweepstep(L, g, GCSswpfinobj, &g->finobj);
			}
		case GCSswpfinobj:
			{  /* sweep objects with finalizers */
				return sweepstep(L, g, GCSswptobefnz, &g->tobefnz);
			}
		case GCSswptobefnz:
			{  /* sweep objects to be finalized */
				return sweepstep(L, g, GCSswpend, NULL);
			}
		case GCSswpend:
			{  /* finish sweeps */
				makewhite(g, g->mainthread);  /* sweep main thread */
				checkSizes(L, g);
				g->gcstate = GCScallfin;
				return 0;
			}
		case GCScallfin:
			{  /* call remaining finalizers */
				if (g->tobefnz && g->gckind != KGC_EMERGENCY)
				{
					int n = runafewfinalizers(L);
					return (n * GCFINALIZECOST);
				}
				else 
				{  /* emergency mode or no more finalizers */
					g->gcstate = GCSpause;  /* finish collection */
					return 0;
				}
			}
		default: lua_assert(0); return 0;
	}
}


/*
 ** advances the garbage collector until it reaches a state allowed
 ** by 'statemask'
 */
void luaC_runtilstate (lua_State *L, int statesmask) {
	global_State *g = G(L);
	while (!testbit(statesmask, g->gcstate))
		singlestep(L);
}


/**
 * 返回值和GCdebt,gcstepmul这两个字段有关
 * gcstepmul是对GCdebt的一个缩放,gcstepmul越大,返回的值越大
 * 说明GC一步要做的工作量越多
 * get GC debt and convert it from Kb to 'work units' (avoid zero debt
 * and overflows)
 **/
static l_mem getdebt (global_State *g) {
	l_mem debt = g->GCdebt;
	int stepmul = g->gcstepmul;
	if (debt <= 0) return 0;  /* minimal debt */
	else {
		debt = (debt / STEPMULADJ) + 1;
		debt = (debt < MAX_LMEM / stepmul) ? debt * stepmul : MAX_LMEM;
		return debt;
	}
}

/*
 ** performs a basic GC step when collector is running
 */
void luaC_step (lua_State *L) {
	global_State *g = G(L);
	// 1. 计算GC的内存债务
	l_mem debt = getdebt(g);  /* GC deficit (be paid now) */
	if (!g->gcrunning) {  /* not running? */
		luaE_setdebt(g, -GCSTEPSIZE * 10);  /* avoid being called too often */
		return;
	}
	// 2. 循环执行singlestep,直到GC周期完毕,或debt小于某个值
	do {  /* repeat until pause or enough "credit" (negative debt) */
		lu_mem work = singlestep(L);  /* perform one single step */
		debt -= work;
	} while (debt > -GCSTEPSIZE && g->gcstate != GCSpause);
	// 3. 如果GC结束,计算下一个阀值
	if (g->gcstate == GCSpause)
		setpause(g);  /* pause until next cycle */
	else {
		// 4. 否则计算下一次触发的时机
		debt = (debt / g->gcstepmul) * STEPMULADJ;  /* convert 'work units' to Kb */
		luaE_setdebt(g, debt);
		runafewfinalizers(L);
	}
}


/*
 ** Performs a full GC cycle; if 'isemergency', set a flag to avoid
 ** some operations which could change the interpreter state in some
 ** unexpected ways (running finalizers and shrinking some structures).
 ** Before running the collection, check 'keepinvariant'; if it is true,
 ** there may be some objects marked as black, so the collector has
 ** to sweep all objects to turn them back to white (as white has not
 ** changed, nothing will be collected).
 */
void luaC_fullgc (lua_State *L, int isemergency) {
	global_State *g = G(L);
	lua_assert(g->gckind == KGC_NORMAL);
	if (isemergency) g->gckind = KGC_EMERGENCY;  /* set flag */
	if (keepinvariant(g)) {  /* black objects? */
		entersweep(L); /* sweep everything to turn them back to white */
	}
	/* finish any pending sweep phase to start a new cycle */
	luaC_runtilstate(L, bitmask(GCSpause));
	luaC_runtilstate(L, ~bitmask(GCSpause));  /* start new collection */
	luaC_runtilstate(L, bitmask(GCScallfin));  /* run up to finalizers */
	/* estimate must be correct after a full GC cycle */
	lua_assert(g->GCestimate == gettotalbytes(g));
	luaC_runtilstate(L, bitmask(GCSpause));  /* finish collection */
	g->gckind = KGC_NORMAL;
	setpause(g);
}

/* }====================================================== */


