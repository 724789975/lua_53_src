/*
** $Id: lstring.h,v 1.61.1.1 2017/04/19 17:20:42 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define sizelstring(l)  (sizeof(union UTString) + ((l) + 1) * sizeof(char))

#define sizeludata(l)	(sizeof(union UUdata) + (l))
#define sizeudata(u)	sizeludata((u)->len)

#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tt == LUA_TSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_TSHRSTR, (a) == (b))


LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC unsigned int luaS_hashlongstr (TString *ts);
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);
LUAI_FUNC void luaS_resize (lua_State *L, int newsize);
/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
** 清楚缓存
*/
LUAI_FUNC void luaS_clearcache (global_State *g);
/*
 ** Initialize the string table and the string cache
 ** 初始化字符串链表和字符串缓存
 */
LUAI_FUNC void luaS_init (lua_State *L);
LUAI_FUNC void luaS_remove (lua_State *L, TString *ts);
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s);
/*
 ** new string (with explicit length)
 ** 创建一个存新的字符串，不带缓存
 ** 字符串不能超过最大限制
 ** 新的字符串会memcpy拷贝一个副本，挂载到TString结构上
 */
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
/*
 ** Create or reuse a zero-terminated string, first checking in the
 ** cache (using the string address as a key). The cache can contain
 ** only zero-terminated strings, so it is safe to use 'strcmp' to
 ** check hits.
 ** 创建一个新的字符串，带缓存方式
 ** 会调用luaS_newlstr方法
 ** 1. 先通过字符串，获取字符串hash值
 ** 2. 通过hash值取字符串，如果相同的字符串已经存在，则复用
 ** 3. 否则创建一个新的字符串
 **
 ** 字符串Table表，通过字符串的hash值找到list
 ** 但是list长度是STRCACHE_M=2，list比较小，估计作者认为hash冲突的概率会非常小
 ** 同时每次都会将最早的元素element淘汰出去
 */
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);
LUAI_FUNC TString *luaS_createlngstrobj (lua_State *L, size_t l);


#endif
