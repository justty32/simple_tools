#ifndef AOS_EXPORT_H
#define AOS_EXPORT_H

/*
 * Symbol visibility for the shared library.
 *
 * Everything else is hidden by default (-fvisibility=hidden), so a symbol
 * is part of the library's surface only by being marked, never by accident.
 * Usable from both C and C++ headers, which is why it lives on its own.
 */

#if defined(_WIN32)
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
