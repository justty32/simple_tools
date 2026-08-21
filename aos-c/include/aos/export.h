#ifndef AOS_EXPORT_H
#define AOS_EXPORT_H

/*
 * Symbol visibility for the shared library.
 *
 * Everything else is hidden by default (-fvisibility=hidden), so a symbol is
 * part of the library's surface only by being marked, never by accident.
 * Usable from both C and C++ headers, which is why it lives on its own.
 *
 * 剩下的分支只是在問「編譯器認不認得 visibility 屬性」，不是在問平台。專案
 * 只支援 POSIX，而 ELF 沒有 Windows 那種 dllexport／dllimport 的分別：同一個
 * 標頭在建置端和取用端讀起來一樣，靜態或動態連結也一樣，取用端不必為此定義
 * 任何東西。
 */

#if defined(__GNUC__)
#define AOS_API __attribute__((visibility("default")))
#else
#define AOS_API
#endif

#endif
