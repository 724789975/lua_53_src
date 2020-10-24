/*
** lua库说明
** lauxlib.c	库编写用到的辅助函数库 
** lbaselib.c	基础库 
** ldblib.c	  Debug 库 
** linit.c		内嵌库的初始化 
** liolib.c	  IO 库 
** lmathlib.c	数学库 
** loadlib.c	动态扩展库管理 
** loslib.c	  OS 库 
** lstrlib.c	字符串库 
** ltablib.c	表处理库
*/

Tvaluefield             ----------> Value                ---------> GCObject              ------------> GCheader              ------------> CommonHeader
Value value ------------|           GCObject *gc --------|          GCheader gch ---------|             CommonHeader ---------|             GCObject *next
int tt                              void *p                         union TString ts                                                        lu_byte tt
                                    lua_Number n                    union Udata u                                                           lu_byte marked
                                    int b                           union Closure cl
                                    lua_CFunction f                 struct Table h
                                    lua_Integer i                   struct Proto p
                                                                    struct UpVal uv
                                                                    struct lua_State th



(1）在非parser函数中,对代码文件的分析返回了Proto指针。 这个指针会保存在Closure指针 中,留待后续继续使用。
(2）在luaD_precall函数中,将lua_state的savedpc指针指向第1步中Proto结构体的code指针, 同时准备好函数调用时的战信息。
(3）在luaV_execute函数中, pc指针指向第2步中的savedpc指针,紧眼着就是一个大的循环体, 依次取出其中的OpCode执行。
执行完毕后,调用luaD_poscall函数恢复到上一个函数的环境

                            ┌───────────────────────┐  
                            │                       │
                            │         Parser        │
                            │                       │
                            └───────────────────────┘
                                        |
                                        |      f_parser
                                        ↓
                            ┌───────────────────────┐  
                            │                       │
                            │         Proto         │
                            │                       │
                            └───────────────────────┘
                                        |
                                        |      luaD_precall: L->savedpc = p->code
                                        ↓
                            ┌───────────────────────┐  
                            │                       │
                            │         savedpc       │
                            │                       │
                            └───────────────────────┘
                                        |
                                        |      luaV_execute: pc = L->savedpc
                                        ↓
                            ┌───────────────────────┐  
                            │                       │
                            │         pc            │
                            │                       │
                            └───────────────────────┘
                                        |
                                        |      luaV_execute 中循环处理OpCode
                                        ↓
                            ┌───────────────────────┐  
                    ┌------>│                       │
                    |       │   循环处理 OpCode      │
                    └-------│                       │
                            └───────────────────────┘

=========================================================
符号                            含义
::=                             推导
{}                              一个或者多个
[]                              出现0次或者1次
|                               或者
=========================================================
chunk ::= block

    block ::= {stat} [retstat]

    stat ::=  ‘;’ | 
         varlist ‘=’ explist | 
         functioncall | 
         label | 
         break | 
         goto Name | 
         do block end | 
         while exp do block end | 
         repeat block until exp | 
         if exp then block {elseif exp then block} [else block] end | 
         for Name ‘=’ exp ‘,’ exp [‘,’ exp] do block end | 
         for namelist in explist do block end | 
         function funcname funcbody | 
         local function Name funcbody | 
         local namelist [‘=’ explist] 

    retstat ::= return [explist] [‘;’]

    label ::= ‘::’ Name ‘::’

    funcname ::= Name {‘.’ Name} [‘:’ Name]

    varlist ::= var {‘,’ var}

    var ::=  Name | prefixexp ‘[’ exp ‘]’ | prefixexp ‘.’ Name 

    namelist ::= Name {‘,’ Name}

    explist ::= exp {‘,’ exp}

    exp ::=  nil | false | true | Numeral | LiteralString | ‘...’ | functiondef | 
         prefixexp | tableconstructor | exp binop exp | unop exp 

    prefixexp ::= var | functioncall | ‘(’ exp ‘)’

    functioncall ::=  prefixexp args | prefixexp ‘:’ Name args 

    args ::=  ‘(’ [explist] ‘)’ | tableconstructor | LiteralString 

    functiondef ::= function funcbody

    funcbody ::= ‘(’ [parlist] ‘)’ block end

    parlist ::= namelist [‘,’ ‘...’] | ‘...’

    tableconstructor ::= ‘{’ [fieldlist] ‘}’

    fieldlist ::= field {fieldsep field} [fieldsep]

    field ::= ‘[’ exp ‘]’ ‘=’ exp | Name ‘=’ exp | exp

    fieldsep ::= ‘,’ | ‘;’

    binop ::=  ‘+’ | ‘-’ | ‘*’ | ‘/’ | ‘//’ | ‘^’ | ‘%’ | 
         ‘&’ | ‘~’ | ‘|’ | ‘>>’ | ‘<<’ | ‘..’ | 
         ‘<’ | ‘<=’ | ‘>’ | ‘>=’ | ‘==’ | ‘~=’ | 
         and | or

    unop ::= ‘-’ | not | ‘#’ | ‘~’
=================================================================

基本算法
基本的垃圾回收算法被称为"mark-and-sweep"算法。算法本身其实很简单。

首先,系统管理着所有已经创建了的对象。每个对象都有对其他对象的引用。root集合代表着已知的系统级别的对象引用。我们从root集合出发,就可以访问到系统引用到的所有对象。而没有被访问到的对象就是垃圾对象,需要被销毁。

我们可以将所有对象分成三个状态：

White状态,也就是待访问状态。表示对象还没有被垃圾回收的标记过程访问到。
Gray状态,也就是待扫描状态。表示对象已经被垃圾回收访问到了,但是对象本身对于其他对象的引用还没有进行遍历访问。
Black状态,也就是已扫描状态。表示对象已经被访问到了,并且也已经遍历了对象本身对其他对象的引用。
基本的算法可以描述如下：

 当前所有对象都是White状态;  
  将root集合引用到的对象从White设置成Gray,并放到Gray集合中;  
  while(Gray集合不为空)  
  {  
      从Gray集合中移除一个对象O,并将O设置成Black状态;  
      for(O中每一个引用到的对象O1) {  
          if(O1在White状态) {  
              将O1从White设置成Gray,并放到到Gray集合中；  
          }  
      }  
 }  
  for(任意一个对象O){  
      if(O在White状态)  
          销毁对象O;  
      else  
          将O设置成White状态;  
  }  
Incremental Garbage Collection
上面的算法如果一次性执行,在对象很多的情况下,会执行很长时间,严重影响程序本身的响应速度。其中一个解决办法就是,可以将上面的算法分步执行,这样每个步骤所耗费的时间就比较小了。我们可以将上述算法改为以下下几个步骤。

首先标识所有的root对象：

1.  当前所有对象都是White状态;  
2.  将root集合引用到的对象从White设置成Gray,并放到Gray集合中;  
遍历访问所有的gray对象。如果超出了本次计算量上限,退出等待下一次遍历:


  while(Gray集合不为空,并且没有超过本次计算量的上限){  
      从Gray集合中移除一个对象O,并将O设置成Black状态;  
      for(O中每一个引用到的对象O1) {  
          if(O1在White状态) {  
              将O1从White设置成Gray,并放到到Gray集合中；  
          }  
      }  
  }  
销毁垃圾对象：

  for(任意一个对象O){  
      if(O在White状态)  
          销毁对象O;  
      else  
          将O设置成White状态;  
 }  

在每个步骤之间,由于程序可以正常执行,所以会破坏当前对象之间的引用关系。black对象表示已经被扫描的对象,所以他应该不可能引用到一个white对象。当程序的改变使得一个black对象引用到一个white对象时,就会造成错误。解决这个问题的办法就是设置barrier。barrier在程序正常运行过程中,监控所有的引用改变。如果一个black对象需要引用一个white对象,存在两种处理办法：

将white对象设置成gray,并添加到gray列表中等待扫描。这样等于帮助整个GC的标识过程向前推进了一步。
将black对象该回成gray,并添加到gray列表中等待扫描。这样等于使整个GC的标识过程后退了一步。
这种垃圾回收方式被称为"Incremental Garbage Collection"(简称为"IGC",Lua所采用的就是这种方法。使用"IGC"并不是没有代价的。IGC所检测出来的垃圾对象集合比实际的集合要小,也就是说,有些在GC过程中变成垃圾的对象,有可能在本轮GC中检测不到。不过,这些残余的垃圾对象一定会在下一轮GC被检测出来,不会造成泄露。


引用关系
垃圾回收过程通过对象之间的引用关系来标识对象。以下是lua对象之间在垃圾回收标识过程中需要遍历的引用关系：
所有字符串对象,无论是长串还是短串,都没有对其他对象的引用。
usedata对象会引用到一个metatable和一个env table。
Upval对象通过v引用一个TValue,再通过这个TValue间接引用一个对象。在open状态下,这个v指向stack上的一个TValue。在close状态下,v指向Upval自己的TValue。
Table对象会通过key,value引用到其他对象,并且如果数组部分有效,也会通过数组部分引用。并且,table会引用一个metatable对象。
Lua closure会引用到Proto对象,并且会通过upvalues数组引用到Upval对象。
C closure会通过upvalues数组引用到其他对象。这里的upvalue与lua closure的upvalue完全不是一个意思。
Proto对象会引用到一些编译期产生的名称,常量,以及内嵌于本Proto中的Proto对象。
Thread对象通过stack引用其他对象。


barrier的位置
lapi.c moveto luaC_barrier
lapi.c lua_load luaC_barrier
lapi.c lua_setupvalue luaC_barrier
lcode.c addk luaC_barrier
lvm.c OP_SETUPVAL luaC_barrier
lapi.c lua_rawset lua_rawseti lua_rawsetp  luaC_barrierback
ltable.c luaH_newkey  luaC_barrierback
lvm.c luaV_settable luaC_barrierback
lvm.c OP_SETLIST luaC_barrierback
lapi.c lua_setmetatable luaC_objbarrier userdate->mt
lapi.c lua_setuservalue luaC_objbarrier
lapi.c lua_upvaluejoin luaC_objbarrier
ldo.c f_parser luaC_objbarrier initialize upvalue
lparser.c luaC_objbarrier
lapi.c lua_setmetatable luaC_objbarrierback


