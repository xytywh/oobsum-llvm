/*
 * 场景用例的公共声明。
 *
 * 风格按 RTOS 业务代码的常见写法（VOS_ 接口、匈牙利命名、安全函数）。
 * 平常编译用真实原型；只在拿 Infer 当标尺时加 -DCASE_SHIM，把它没有模型的
 * 分配器和安全函数换成它认识的 malloc / memset（只检验 destMax <= ext(dst) 那一半）。
 */
#ifndef CASE_COMMON_H
#define CASE_COMMON_H

#include <stddef.h>

typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;
typedef signed int     INT32;
typedef char           CHAR;
typedef int            errno_t;

#define VOS_OK        0
#define VOS_ERR       1
#define VOS_TRUE      1
#define VOS_FALSE     0
#define EOK           0
#define VOS_NULL_PTR  ((void *)0)
#define PID_CELL      0x1001

#ifdef CASE_SHIM
#include <stdlib.h>
#include <string.h>
#define VOS_MemAlloc(pid, pt, sz)  malloc(sz)
#define VOS_MemFree(pid, p)        (free(p), 0)
#define memcpy_s(d, dm, s, c)      (memset((d), 0, (dm)), 0)
#define memmove_s(d, dm, s, c)     (memset((d), 0, (dm)), 0)
#define memset_s(d, dm, v, c)      (memset((d), 0, (dm)), 0)
#define strcpy_s(d, dm, s)         (memset((d), 0, (dm)), 0)
#else
/* 底层分配器：第 3 个形参是字节数（01- §3.7.1 的假设，以真实原型为准） */
void   *VOS_MemAlloc(UINT32 ulPid, UINT32 ulPtNo, UINT32 ulSize);
UINT32  VOS_MemFree(UINT32 ulPid, void *pAddr);
errno_t memcpy_s(void *dest, size_t destMax, const void *src, size_t count);
errno_t memmove_s(void *dest, size_t destMax, const void *src, size_t count);
errno_t memset_s(void *dest, size_t destMax, int c, size_t count);
errno_t strcpy_s(char *dest, size_t destMax, const char *src);
#endif

/* 日志：不接收任何会流进内存访问的东西，摘要里不会出现它 */
void LOG_Print(UINT32 ulLevel, const CHAR *pszFmt, ...);

/* 业务里常见的一层薄封装分配器（Lv4 的用例里还会再套几层） */
static inline void *Pub_MemAlloc(UINT32 ulSize)
{
    return VOS_MemAlloc(PID_CELL, 0, ulSize);
}

#endif
