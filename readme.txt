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

Tvaluefield             ----------> Value                 --------> GCObject               -----------> GCheader               -----------> CommonHeader
Value value ------------|           GCObject *gc --------|          GCheader gch ---------|             CommonHeader ---------|             GCObject *next
int tt                              void *p                         union TString ts                                                        lu_byte tt
                                    lua Number n                    union Udata u                                                           lu_byte marked
                                    int b                           union Closure cl
                                                                    struct Table h
                                                                    struct Proto p
                                                                    struct UpVal uv
                                                                    struct lua State th


