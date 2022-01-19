/*
 * $Id: config.h.win32,v 1.2 2006/01/26 02:16:28 mclark Exp $
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.
 *
 */

/* config.h.win32  Generated by configure.  */

#define PACKAGE_STRING "JSON C Library 0.2"
#define PACKAGE_BUGREPORT "michael@metaparadigm.com"
#define PACKAGE_NAME "JSON C Library"
#define PACKAGE_TARNAME "json-c"
#define PACKAGE_VERSION "0.2"

#include "symbol_renames.h"

/* config.h.in.  Generated from configure.ac by autoheader.  */

#ifndef __GNUC__
#define __attribute__(x) /* DO NOTHING */
#endif

/* Define to 1 if you don't have `vprintf' but do have `_doprnt.' */
/* #undef HAVE_DOPRNT */

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if your system has a GNU libc compatible `malloc' function, and
   to 0 otherwise. */
#define HAVE_MALLOC 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `open' function. */
#undef HAVE_OPEN

/* Define to 1 if your system has a GNU libc compatible `realloc' function,
   and to 0 otherwise. */
#define HAVE_REALLOC 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the `strerror' function. */
#ifndef HAVE_STRERROR
#define HAVE_STRERROR 1
#endif

/* Define to 1 if you have the <strings.h> header file. */
#undef HAVE_STRINGS_H

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <syslog.h> header file. */
#undef HAVE_SYSLOG_H

/* Define to 1 if you have the <sys/param.h> header file. */
#undef HAVE_SYS_PARAM_H

/* Define to 1 if you have the <sys/stat.h> header file. */
#ifdef _WIN32_WCE
#undef HAVE_SYS_STAT_H
#else
#define HAVE_SYS_STAT_H 1
#endif

/* Define to 1 if you have the <sys/types.h> header file. */
#ifdef _WIN32_WCE
#undef HAVE_SYS_TYPES_H
#else
#define HAVE_SYS_TYPES_H 1
#endif

/* Define to 1 if you have the <unistd.h> header file. */
#ifndef _WIN32
#define HAVE_UNISTD_H 1
#endif

/* Define to 1 if you have the `vprintf' function. */
#ifdef _MSC_VER
#undef HAVE_VPRINTF
#endif

/* Define to 1 if you have the `vasprintf' function. */
#define HAVE_VASPRINTF 1
#ifdef _MSC_VER
#undef HAVE_VASPRINTF
#endif

/* Define to 1 if you have the `vsyslog' function. */
#undef HAVE_VSYSLOG

/* Define to 1 if you have the `strncasecmp' function. */
#ifndef HAVE_STRNCASECMP
#define HAVE_STRNCASECMP 1
#endif
#if defined(_MSC_VER) && !defined(strncasecmp)
   /* MSC has the version as _strnicmp */
#define strncasecmp _strnicmp
#endif

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

#define HAVE_DECL_ISNAN
#define HAVE_DECL_ISINF
#define HAVE_DECL_INFINITY
#define HAVE_DECL_NAN

#include "cpl_config.h"
