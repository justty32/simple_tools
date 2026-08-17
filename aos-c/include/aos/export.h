#ifndef AOS_EXPORT_H
#define AOS_EXPORT_H

/*
 * Symbol visibility for the shared library.
 *
 * Everything else is hidden by default (-fvisibility=hidden), so a symbol is
 * part of the library's surface only by being marked, never by accident.
 * Usable from both C and C++ headers, which is why it lives on its own.
 *
 * Windows needs three states rather than two, because __declspec(dllimport)
 * makes the compiler emit __imp_ indirections that only a DLL can satisfy:
 *
 *   AOS_BUILDING_LIBRARY   建置 DLL 本身          -> dllexport
 *   AOS_STATIC             直接連結目的檔或靜態庫 -> 什麼都不加
 *   （都沒定義）           連結 DLL 的取用端      -> dllimport
 *
 * 所以靜態連結的取用端必須定義 AOS_STATIC。忘了定義的話會是連結期錯誤
 * （找不到 __imp_aos_*），不會是難查的執行期問題。
 */

#if defined(_WIN32) && !defined(AOS_STATIC)
#if defined(AOS_BUILDING_LIBRARY)
#define AOS_API __declspec(dllexport)
#else
#define AOS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define AOS_API __attribute__((visibility("default")))
#else
#define AOS_API
#endif

#endif
