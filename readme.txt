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



(1）在飞parser函数中，对代码文件的分析返回了Proto指针。 这个指针会保存在Closure指针 中，留待后续继续使用。
(2）在luaD_precall函数中，将lua_state的savedpc指针指向第1步中Proto结构体的code指针， 同时准备好函数调用时的战信息。
(3）在luaV_execute函数中， pc指针指向第2步中的savedpc指针，紧眼着就是一个大的循环体， 依次取出其中的OpCode执行。
执行完毕后，调用luaD_poscall函数恢复到上一个函数的环境

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










