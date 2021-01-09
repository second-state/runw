// SPDX-License-Identifier: Apache-2.0

#pragma once
#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) ||                \
    defined(__TOS_WIN__) || defined(__WINDOWS__)
#define RUNW_OS_WINDOWS 1
#endif

#if defined(linux) || defined(__linux) || defined(__linux__) ||                \
    defined(__gnu_linux__)
#define RUNW_OS_LINUX 1
#endif

#if defined(sun) || defined(__sun)
#define RUNW_OS_SOLARIS 1
#endif

#if defined(macintosh) || defined(Macintosh) ||                                \
    (defined(__APPLE__) && defined(__MACH__))
#define RUNW_OS_MACOS 1
#endif
