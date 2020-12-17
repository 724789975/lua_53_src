/*
** $Id: lmem.h,v 1.43.1.1 2017/04/19 17:20:42 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <stddef.h>

#include "llimits.h"
#include "lua.h"


/*
** This macro reallocs a vector 'b' from 'on' to 'n' elements, where
** each element has size 'e'. In case of arithmetic overflow of the
** product 'n'*'e', it raises an error (calling 'luaM_toobig'). Because
** 'e' is always constant, it avoids the runtime division MAX_SIZET/(e).
**
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
**
** 将使数组b的长度(最大容纳元素个数)从on重新分配为n,其中每个数组元素大小为e
** b　　：数组指针
** on　 ：数组重新分配前的长度(最大容纳元素个数)
** n  　 ：数组重新分配后的长度(最大容纳元素个数)
** e　　：数组元素大小
*/
#define luaM_reallocv(L,b,on,n,e) \
  (((sizeof(n) >= sizeof(size_t) && cast(size_t, (n)) + 1 > MAX_SIZET/(e)) \
      ? luaM_toobig(L) : cast_void(0)) , \
   luaM_realloc_(L, (b), (on)*(e), (n)*(e)))

/**
 * Arrays of chars do not need any test
 * luaM_reallocvchar将使字符数组b的长度(最大容纳元素个数)从on重新分配为n,其中每个数组元素大小为sizeof(char)
 * b　　：数组指针
 * on　 ：数组重新分配前的长度(最大容纳元素个数)
 * n　　：数组重新分配后的长度(最大容纳元素个数)
*/
#define luaM_reallocvchar(L,b,on,n)  \
    cast(char *, luaM_realloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

/**
 * luaM_freemem将释放b指向的内存块空间
 *  b　　：内存块指针
 *  s　    ：内存块大小
*/
#define luaM_freemem(L, b, s)	luaM_realloc_(L, (b), (s), 0)
/**
 * luaM_free将释放b指向的内存块空间(b表示某种对象类型指针)
 * b　　：内存指针,同时表示某种对象类型指针
*/
#define luaM_free(L, b)		luaM_realloc_(L, (b), sizeof(*(b)), 0)
/**
 * luaM_freearray将释放b指向的内存块空间(b表示某种类型对象的数组指针)
 * b　　：内存指针,同时表示某种类型对象的数组指针
 * n　　：数组长度(最大容纳元素个数)
 */
#define luaM_freearray(L, b, n)   luaM_realloc_(L, (b), (n)*sizeof(*(b)), 0)

/**
 * luaM_malloc将分配一块大小为s的内存块空间
 * s　　：将要分配的内存块空间大小
 */
#define luaM_malloc(L,s)	luaM_realloc_(L, NULL, 0, (s))
/**
 * luaM_new将分配一块内存块空间,空间大小为sizeof(t)。
 * t　　：某种数据类型
 */
#define luaM_new(L,t)		cast(t *, luaM_malloc(L, sizeof(t)))
/**
 * luaM_newvector将分配一个长度为n的数组空间,数组元素为类型t
 * n　　：数组长度(最大容纳元素个数)
 * t　　：数组元素类型
 */
#define luaM_newvector(L,n,t) \
		cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))

/**
 * luaM_newobject将分配一块大小为s的内存块空间,其将要容纳的Lua数据类型为tag表示的类型
 * tag   ：Lua数据类型
 * s　　：分配的内存块大小
 */
#define luaM_newobject(L,tag,s)	luaM_realloc_(L, NULL, tag, (s))

/**
 * luaM_growvector将在数组空间不足以容纳下一个元素的情况下增长空间大小(原空间大小 * 2)
 * v        ：数组指针
 * nelems   ：正在使用的元素个数
 * size  　　：数组元素个数,传入表示原始数组大小,传出表示重新分配后数组大小
 *  t       　　：(数组元素的)数据类型
 * limit 　　：数组元素最大个数限制
 * e　　　　：提示信息字符串
 */
#define luaM_growvector(L,v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=cast(t *, luaM_growaux_(L,v,&(size),sizeof(t),limit,e)))

/**
 * luaM_reallocvector将重新分配数组空间大小
 * v　　：数组指针
 * oldn ：重新分配前数组大小
 * n　　：重新分配后数组大小
 */
#define luaM_reallocvector(L, v,oldn,n,t) \
   ((v)=cast(t *, luaM_reallocv(L, v, oldn, n, sizeof(t))))

LUAI_FUNC l_noret luaM_toobig (lua_State *L);

/* not to be called directly */
/*
** generic allocation routine.
** 内存分配函数
** 内容快最终会调用*g->frealloc(*l_alloc)函数处理
**
** osize = 老的内存块大小  这参数没用上
**
*/
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int *size,
                               size_t size_elem, int limit,
                               const char *what);

#endif

