/* zconf.h -- configuration of the zlib compression library
 * zlib 1.2.8 Windows x86 minimal configuration
 * （為本專案手動補充，省去 zlib 完整安裝需求）
 *
 * 本檔案僅提供「型別定義」與「匯出巨集」等平台組態，
 * 常數（Z_OK / Z_NO_FLUSH / Z_BEST_SPEED …）統一由 zlib.h 定義，
 * 請勿在此重複宣告以避免巨集重定義警告。
 */

#ifndef ZCONF_H
#define ZCONF_H

#ifndef ZLIB_INTERNAL
#  define ZLIB_INTERNAL
#endif

/* Maximum value for memLevel in deflateInit2 */
#ifndef MAX_MEM_LEVEL
#  define MAX_MEM_LEVEL 9
#endif

/* Maximum value for windowBits in deflateInit2 and inflateInit2 */
#ifndef MAX_WBITS
#  define MAX_WBITS 15 /* 32K LZ77 window */
#endif

#ifndef FAR
#  define FAR
#endif

/* Basic type definitions */
typedef unsigned char  Byte;   /* 8 bits  */
typedef unsigned int   uInt;   /* 16 bits or more */
typedef unsigned long  uLong;  /* 32 bits or more */

typedef Byte   FAR Bytef;
typedef char   FAR charf;
typedef int    FAR intf;
typedef uInt   FAR uIntf;
typedef uLong  FAR uLongf;

typedef void const *voidpc;
typedef void  FAR  *voidpf;
typedef void       *voidp;

/* z_off_t: file offset type */
#ifdef _WIN64
  typedef __int64    z_off_t;
  typedef __int64    z_off64_t;
#else
  typedef long       z_off_t;
  typedef __int64    z_off64_t;
#endif

/* 現代編譯器偵測（MSVC / C++ 均視為 ANSI 相容） */
#if !defined(STDC) && (defined(__STDC__) || defined(__cplusplus) || defined(_MSC_VER))
#  define STDC
#endif

/* Windows DLL 匯出巨集（zlib.h 函式宣告所需） */
#if defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#  ifdef ZLIB_DLL
#    define ZEXTERN  extern __declspec(dllexport)
#    define ZEXPORT  __cdecl
#  else
#    define ZEXTERN  extern
#    define ZEXPORT  __cdecl
#  endif
#  define ZEXPORTVA  __cdecl
#else
#  define ZEXTERN  extern
#  define ZEXPORT
#  define ZEXPORTVA
#endif

/* z_const: 用於標記 z_stream.next_in 等唯讀指標（C89 相容） */
#define z_const const

/* z_crc_t: CRC-32 計算用型別 */
typedef unsigned long z_crc_t;

/* OF 巨集: 為 K&R C 相容的函式原型宣告（現代編譯器直接展開） */
#ifndef OF
#  define OF(args) args
#endif

/* Z_ARG 巨集: 可變參數函式原型宣告 */
#ifndef Z_ARG
#  ifdef STDC
#    define Z_ARG(args) args
#  else
#    define Z_ARG(args) ()
#  endif
#endif

/* 作業系統識別碼 */
#ifndef OS_CODE
#  define OS_CODE  0x0b  /* Windows */
#endif

#endif /* ZCONF_H */
