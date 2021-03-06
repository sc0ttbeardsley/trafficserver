/** @file

  Memory allocation routines for libts

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */
#include "libts.h"

#include <assert.h>
#if defined(linux)
// XXX: SHouldn't that be part of CPPFLAGS?
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#endif
#include <stdlib.h>
#include <string.h>

void *
ats_malloc(size_t size)
{
  void *ptr = NULL;

  /*
   * There's some nasty code in libts that expects
   * a MALLOC of a zero-sized item to work properly. Rather
   * than allocate any space, we simply return a NULL to make
   * certain they die quickly & don't trash things.
   */

  // Useful for tracing bad mallocs
  // ink_stack_trace_dump();
  if (likely(size > 0)) {
    if (unlikely((ptr = malloc(size)) == NULL)) {
      ink_stack_trace_dump();
      ink_fatal(1, "ats_malloc: couldn't allocate %zu bytes", size);
    }
  }
  return ptr;
}                               /* End ats_malloc */

void *
ats_calloc(size_t nelem, size_t elsize)
{
  void *ptr = calloc(nelem, elsize);
  if (unlikely(ptr == NULL)) {
    ink_stack_trace_dump();
    ink_fatal(1, "ats_calloc: couldn't allocate %zu %zu byte elements", nelem, elsize);
  }
  return ptr;
}                               /* End ats_calloc */

void *
ats_realloc(void *ptr, size_t size)
{
  void *newptr = realloc(ptr, size);
  if (unlikely(newptr == NULL)) {
    ink_stack_trace_dump();
    ink_fatal(1, "ats_realloc: couldn't reallocate %zu bytes", size);
  }
  return newptr;
}                               /* End ats_realloc */

// TODO: For Win32 platforms, we need to figure out what to do with memalign.
// The older code had ifdef's around such calls, turning them into ats_malloc().
void *
ats_memalign(size_t alignment, size_t size)
{
  void *ptr;

#if HAVE_POSIX_MEMALIGN || TS_HAS_JEMALLOC
  if (alignment <= 8)
    return ats_malloc(size);

#if defined(openbsd)
  if (alignment > PAGE_SIZE)
      alignment = PAGE_SIZE;
#endif

  int retcode = posix_memalign(&ptr, alignment, size);

  if (unlikely(retcode)) {
    if (retcode == EINVAL) {
      ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu - invalid alignment parameter",
                size, alignment);
    } else if (retcode == ENOMEM) {
      ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu - insufficient memory",
                size, alignment);
    } else {
      ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu - unknown error %d",
                size, alignment, retcode);
    }
  }
#else
  ptr = memalign(alignment, size);
  if (unlikely(ptr == NULL)) {
    ink_fatal(1, "ats_memalign: couldn't allocate %zu bytes at alignment %zu",  size,  alignment);
  }
#endif
  return ptr;
}                               /* End ats_memalign */

void
ats_free(void *ptr)
{
  if (likely(ptr != NULL))
    free(ptr);
}                               /* End ats_free */

void*
ats_free_null(void *ptr)
{
  if (likely(ptr != NULL))
    free(ptr);
  return NULL;
}                               /* End ats_free_null */

void
ats_memalign_free(void *ptr)
{
  if (likely(ptr))
    free(ptr);
}

// This effectively makes mallopt() a no-op (currently) when tcmalloc
// or jemalloc is used. This might break our usage for increasing the
// number of mmap areas (ToDo: Do we still really need that??).
//
// TODO: I think we might be able to get rid of this?
int
ats_mallopt(int param ATS_UNUSED, int value ATS_UNUSED)
{
#if TS_HAS_JEMALLOC
// TODO: jemalloc code ?
#else
#if TS_HAS_TCMALLOC
// TODO: tcmalloc code ?
#else
#if defined(linux)
  return mallopt(param, value);
#endif // ! defined(linux)
#endif // ! TS_HAS_TCMALLOC
#endif // ! TS_HAS_JEMALLOC
  return 0;
}

int
ats_msync(caddr_t addr, size_t len, caddr_t end, int flags)
{
  size_t pagesize = ats_pagesize();

  // align start back to page boundary
  caddr_t a = (caddr_t) (((uintptr_t) addr) & ~(pagesize - 1));
  // align length to page boundry covering region
  size_t l = (len + (addr - a) + (pagesize - 1)) & ~(pagesize - 1);
  if ((a + l) > end)
    l = end - a;                // strict limit
#if defined(linux)
/* Fix INKqa06500
   Under Linux, msync(..., MS_SYNC) calls are painfully slow, even on
   non-dirty buffers. This is true as of kernel 2.2.12. We sacrifice
   restartability under OS in order to avoid a nasty performance hit
   from a kernel global lock. */
#if 0
  // this was long long ago
  if (flags & MS_SYNC)
    flags = (flags & ~MS_SYNC) | MS_ASYNC;
#endif
#endif
  int res = msync(a, l, flags);
  return res;
}

int
ats_madvise(caddr_t addr, size_t len, int flags)
{
#if defined(linux)
  (void) addr;
  (void) len;
  (void) flags;
  return 0;
#else
  size_t pagesize = ats_pagesize();
  caddr_t a = (caddr_t) (((uintptr_t) addr) & ~(pagesize - 1));
  size_t l = (len + (addr - a) + pagesize - 1) & ~(pagesize - 1);
  int res = 0;
#if HAVE_POSIX_MADVISE
  res = posix_madvise(a, l, flags);
#else
  res = madvise(a, l, flags);
#endif
  return res;
#endif
}

int
ats_mlock(caddr_t addr, size_t len)
{
  size_t pagesize = ats_pagesize();

  caddr_t a = (caddr_t) (((uintptr_t) addr) & ~(pagesize - 1));
  size_t l = (len + (addr - a) + pagesize - 1) & ~(pagesize - 1);
  int res = mlock(a, l);
  return res;
}


/*-------------------------------------------------------------------------
  Moved from old ink_resource.h
  -------------------------------------------------------------------------*/
char *
_xstrdup(const char *str, int length, const char* /* path ATS_UNUSED */)
{
  char *newstr;

  if (likely(str)) {
    if (length < 0)
      length = strlen(str);

    newstr = (char *)ats_malloc(length + 1);
    // If this is a zero length string just null terminate and return.
    if (unlikely(length == 0)) {
      *newstr = '\0';
    } else {
      ink_strlcpy(newstr, str, length + 1);
    }
    return newstr;
  }
  return NULL;
}
