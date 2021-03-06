/* Copyright (c) 2015, Jorge Acereda Maciá <jacereda@gmail.com>
   Copyright (C) 2016 David Roundy <daveroundy@gmail.com>

   Permission to use, copy, modify, and/or distribute this software
   for any purpose with or without fee is hereby granted, provided
   that the above copyright notice and this permission notice appear
   in all copies.

   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
   AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
   OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

// We need to assert that we are running on Vista or later in order to
// get the GetFinalPathNameByHandleW declaration.
#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <winternl.h>
#include <limits.h>
#include <wchar.h>

#undef ASSERT
/* #include "handle.h" */
/* #include "utf8.h" */
#include "patch.h"
#include "inject.h"
#include "hooks.h"
#include "queue.h"
#include "../errors.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Convert a wide Unicode string to an UTF8 string
void utf8_encode(char *buf, int buflen, const wchar_t *input, int inputlen) {
  if (inputlen == 0) {
    *buf = 0;
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, input, inputlen, buf, buflen, NULL, NULL);
}

// Convert an UTF8 string to a wide Unicode String
void utf8_decode(wchar_t *buf, int buflen, const char *input, int inputlen) {
  if (inputlen == 0) {
    *buf = 0;
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, input, inputlen, buf, buflen);
}

static char *utf8PathFromWide(char *buf, const PWSTR s, int sl) {
  if (sl <= 0) return 0;
  int l;
  l = WideCharToMultiByte(CP_UTF8, 0, s, sl, buf, PATH_MAX, 0, 0);
  if (l == 0) return 0;
  buf[l] = 0;
  if (!buf[0]) return 0;
  if (buf[0] == '\\' && !strchr(buf, ':')) return 0;
  if (strncmp(buf, "\\\\?\\", 4) == 0 || strncmp(buf, "\\??\\", 4) == 0)
    return buf + 4;
  return buf;
}

static inline char *handlePath(char *dst, HANDLE h) {
  WCHAR wbuf[PATH_MAX];
  int len = GetFinalPathNameByHandleW(h, wbuf, PATH_MAX, FILE_NAME_NORMALIZED);
  if (len == 0) {
    switch (GetLastError()) {
    case ERROR_INVALID_FUNCTION:
      printf("error in GetFinalPathNameByHandleW: bad function for handle %p?!\n", h);
      break;
    case ERROR_PATH_NOT_FOUND:
      printf("error in GetFinalPathNameByHandleW: path not found\n");
      break;
    case ERROR_NOT_ENOUGH_MEMORY:
      printf("error in GetFinalPathNameByHandleW: not enough memory\n");
      break;
    case ERROR_INVALID_HANDLE:
      printf("error in GetFinalPathNameByHandleW: the handle is invalid\n");
      break;
    case ERROR_INVALID_PARAMETER:
      printf("error in GetFinalPathNameByHandleW: bad parameter\n");
      break;
    default:
      printf("error in GetFinalPathNameByHandleW: i don't understand error %d\n",
             (int) GetLastError());
    }
    return 0;
  }
  if (len >= PATH_MAX) {
    printf("error in GetFinalPathNameByHandleW: PATH_MAX was not long enough...\n");
    return 0;
  }
  return utf8PathFromWide(dst, wbuf, len);
}

#define HOOK(n) static NTSTATUS(NTAPI *o##n)()
HOOK(NtCreateFile);
HOOK(NtOpenFile);
/* HOOK(NtDeleteFile); */
HOOK(NtSetInformationFile);
HOOK(NtQueryFullAttributesFile);
HOOK(NtQueryInformationFile);
HOOK(NtResumeThread);
#undef HOOK

#if defined _MSC_VER
/* typedef struct _FILE_STANDARD_INFORMATION { */
/* 	LARGE_INTEGER AllocationSize; */
/* 	LARGE_INTEGER EndOfFile; */
/* 	ULONG         NumberOfLinks; */
/* 	BOOLEAN       DeletePending; */
/* 	BOOLEAN       Directory; */
/* } FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION; */

/* NTSTATUS NTAPI NtQueryInformationFile( */
/* 	_In_  HANDLE                 FileHandle, */
/* 	_Out_ PIO_STATUS_BLOCK       IoStatusBlock, */
/* 	_Out_ PVOID                  FileInformation, */
/* 	_In_  ULONG                  Length, */
/* 	_In_  FILE_INFORMATION_CLASS FileInformationClass */
/* 	); */

/* enum { FileBasicInformation = 4, */
/*        FileRenameInformation = 10, */
/*        FileDispositionInformation = 13, */
/*        FileAllocationInformation = 19, */
/* }; */

#endif

static const char fop(ULONG co, ACCESS_MASK am, const char *name) {
  if (co & FILE_DIRECTORY_FILE) {
    printf("FILE_DIRECTORY_FILE %s\n", name);
    return 0;
  } else if (co & FILE_DELETE_ON_CLOSE) {
    printf("FILE_DELETE_ON_CLOSE %s\n", name);
    return 0;
  } else if (am & GENERIC_WRITE) {
    printf("GENERIC_WRITE %s\n", name);
    return WRITE_OP;
  } else if (am & GENERIC_READ) {
    printf("GENERIC_READ %s\n", name);
    return READ_OP;
  }
  printf("what fop ??? %s\n", name);
  return 0;
}

static NTSTATUS NTAPI hNtCreateFile(PHANDLE ph,
                                    ACCESS_MASK am,
                                    POBJECT_ATTRIBUTES oa,
                                    PIO_STATUS_BLOCK sb,
                                    PLARGE_INTEGER as,
                                    ULONG fa,
                                    ULONG sa,
                                    ULONG cd,
                                    ULONG co,
                                    PVOID bu,
                                    ULONG le) {
  NTSTATUS r;
  r = oNtCreateFile(ph, am, oa, sb, as, fa, sa, cd, co, bu, le);
  if (NT_SUCCESS(r)) {
    char buf[4096];
    char *p = handlePath(buf, *ph);
    if (!p) {
      printf("handlePath gives a null path pointer!\n");
      return r;
    }
    if (p) {
      printf("I am in hNtCreateFile with string %s and handle %p!\n", p,  *ph);
      char op = fop(co, am, p);
      if (op) queueOp(op, p);
    } else {
      printf("I am in hNtCreateFile with string (nil) and handle %p!\n", *ph);
    }
  }
  return r;
}

static NTSTATUS NTAPI hNtOpenFile(PHANDLE ph,
                                  ACCESS_MASK am,
                                  POBJECT_ATTRIBUTES oa,
                                  PIO_STATUS_BLOCK sb,
                                  ULONG sa,
                                  ULONG oo) {
  NTSTATUS r;
  printf("am in hNtOpenFile!\n");
  r = oNtOpenFile(ph, am, oa, sb, sa, oo);
  if (NT_SUCCESS(r)) {
    char buf[4096];
    char *p = handlePath(buf, *ph);
    if (!p) {
      printf("handlePath gives a null path pointer!\n");
      return r;
    }
    if (p) {
      printf("I am in hNtOpenFile with string %s and handle %p!\n", p, *ph);
      char op = fop(oo, am, p);
      if (op) queueOp(op, p);
    } else {
      printf("I am in hNtOpenFile with string (nil) and handle %p!\n", *ph);
    }
  } else {
    printf("no success in hNtOpenFile\n");
  }
  return r;
}

/* static NTSTATUS NTAPI hNtDeleteFile(POBJECT_ATTRIBUTES oa) { */
/*   NTSTATUS r; */
/*   debugprintf("am in hNtDeleteFile!\n"); */
/*   r = oNtDeleteFile(oa); */
/*   if (NT_SUCCESS(r)) { */
/*     // we do not actually track file deletions... */
/*   } */
/*   return r; */
/* } */

static NTSTATUS NTAPI hNtSetInformationFile(HANDLE fh,
                                            PIO_STATUS_BLOCK sb,
                                            PVOID fi,
                                            ULONG ln,
                                            FILE_INFORMATION_CLASS ic) {
  printf("am in hNtSetInformationFile!\n");
  NTSTATUS r;
  char buf[PATH_MAX];
  char buf2[PATH_MAX];
#ifdef _MSC_VER
  PFILE_RENAME_INFO ri = (PFILE_RENAME_INFO)fi;
#else
  PFILE_RENAME_INFORMATION ri = (PFILE_RENAME_INFORMATION)fi;
#endif
  char * opath = handlePath(buf, fh);
  r = oNtSetInformationFile(fh, sb, fi, ln, ic);
  if (NT_SUCCESS(r)) {
    switch (ic) {
    case FileBasicInformation:
      queueOp(SETINFO_OP, opath);
      break;
    case FileRenameInformation:
      {
        const char *npath = utf8PathFromWide(buf2, ri->FileName, ri->FileNameLength/2);
        queueOp2(RENAME_OP, opath, npath);
      }
      break;
    case FileDispositionInformation:
      // used for deleting files
      break;
    case FileAllocationInformation:
      // windows version of truncate
      queueOp(WRITE_OP, opath);
      break;
    default:
      break;
    }
  }
  return r;
}

static NTSTATUS NTAPI hNtQueryInformationFile(HANDLE fh,
                                              PIO_STATUS_BLOCK sb,
                                              PVOID fi,
                                              ULONG ln,
                                              FILE_INFORMATION_CLASS ic) {
  printf("am in hNtQueryInformationFile with handle %p!\n", fh);
  NTSTATUS r;
  char buf[PATH_MAX];
  r = oNtQueryInformationFile(fh, sb, fi, ln, ic);
  if (NT_SUCCESS(r)) {
    switch (ic) {
    case FileAllInformation:
    case FileNetworkOpenInformation:
      queueOp(GETINFO_OP, handlePath(buf, fh));
      break;
    default:
      break;
    }
  }
  return r;
}


static NTSTATUS NTAPI hNtQueryFullAttributesFile(POBJECT_ATTRIBUTES oa, PFILE_NETWORK_OPEN_INFORMATION oi) {
  printf("am in hNtQueryFullAttributesFile!\n");
  NTSTATUS r;
  r = oNtQueryFullAttributesFile(oa, oi);
  if (NT_SUCCESS(r)) {
    char buf[PATH_MAX];
    queueOp(GETINFO_OP, utf8PathFromWide(buf, oa->ObjectName->Buffer, oa->ObjectName->Length/2));
  }
  return r;
}

static NTSTATUS NTAPI hNtResumeThread(HANDLE th, PULONG sc) {
  debugprintf("am in hNtResumeThread!\n");
  NTSTATUS r;
  if (!patchInstalled()) {
    injectThread(th);
  }
  r = oNtResumeThread(th, sc);
  return r;
}


void hooksInit(void *(*resolve)(const char *)) {
	void * addr;
#define HOOK(n)							\
	addr = resolve(#n);					\
	patchInstall(addr, (void *)h##n, (void **) &o##n, #n)

	HOOK(NtCreateFile);
	HOOK(NtOpenFile);
	/* HOOK(NtDeleteFile); */
	HOOK(NtSetInformationFile);
	HOOK(NtQueryFullAttributesFile);
	HOOK(NtQueryInformationFile);
	HOOK(NtResumeThread);
#undef HOOK
}
