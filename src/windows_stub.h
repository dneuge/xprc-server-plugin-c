#ifndef XPRC_WINDOWS_STUB_H
#define XPRC_WINDOWS_STUB_H

#ifdef TARGET_WINDOWS
#error "compiling for Windows but windows_stub.h has been mistakenly included where it should not be used"
#else

/* The goal of this file is to establish IDE compatibility for files holding Windows-specific implementations
 * which lack Windows headers.
 *
 * Headers are vague approximations, just enough to satisfy IDEs so general code analysis is still possible.
 * The definitions in this file are by far not accurate and omit a lot of information.
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!! DO NOT USE THIS FILE AS REFERENCE !!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * This file is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
 *
 *   https://github.com/MicrosoftDocs/sdk-api
 *   revision 586165cc8a117fdce141de4c8a3b4bb8be9d7cae (12 Jul 2022)
 *   AI rationale: later revisions look like they may have been partially affected by AI tooling, commits up to
 *                 early July 2022 seem "normal" at first glance
 *   Deep links:
 *   https://github.com/MicrosoftDocs/sdk-api/tree/586165cc8a117fdce141de4c8a3b4bb8be9d7cae/sdk-api-src/content/winuser
 *
 *
 *   https://github.com/MicrosoftDocs/win32
 *   license checked at revision 192df369a6bdb7d290ddb47187c8371507dc6c0a (20 Mar 2026)
 *   see individual revisions/AI rationales below
 *
 *   see repositories at specified revisions for detailed license information
 *
 *
 * This file itself remains published under MIT license. If one of the API reference sources requires a more
 * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
 * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
 * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
 * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
 * the original API docs on your own instead.
 */

// constant expected to be returned by GetLastError - we don't actually care what it is, it just has to be defined
#define NO_ERROR 0

// returns last error, check for NO_ERROR - no idea what the actual return value is but it's probably some number
int GetLastError();

// memory marked as relocatable/reusable - we don't know and don't care about the exact value, this is probably wrong
#define GMEM_MOVEABLE 0

// it seems that HGLOBAL (a global handle of some sort) is probably a memory pointer alias implicitly including *
#define HGLOBAL void*

// window handles (?) are probably a pointer as documentation refers to NULL being usable (guessed, not looked up)
#define HWND void*

// allocation of memory referenced through a globally shareable handle - type signature not specified by documentation
HGLOBAL GlobalAlloc(int flags, unsigned long long size);

// locks a global handle and returns a memory pointer if locked
void* GlobalLock(HGLOBAL handle);

// unlocks a global handle, return value does not matter - use GetLastError to check if successful/failed
int GlobalUnlock(HGLOBAL handle);

// frees a handle, return value is the handle itself if it has not been freed, otherwise NULL
HGLOBAL GlobalFree(HGLOBAL handle);

// opens the clipboard in context of the given owner which may be NULL/0; return value is a success boolean
int OpenClipboard(HWND owner);

// empties the open clipboard; return value is a success boolean
int EmptyClipboard();

// closes the clipboard; return value is a success boolean (whatever is supposed to fail here)
int CloseClipboard();

// transfers memory to clipboard, clipboard takes over memory management if successful - returns NULL on error
HGLOBAL SetClipboardData(unsigned int format, HGLOBAL memory_handle);

// clipboard format constant identifying ANSI-encoded plain-text - this is probably not the real value, only present for type definition
//
// CF_TEXT exists and is used for ANSI according to:
//   [win32] ref 34ebbe3e55bf07700918cee1452aa0d35f9dff01 /desktop-src/dataxchg/standard-clipboard-formats.md
// AI rationale: name is also seen in other sources
#define CF_TEXT 0

// codepage identifier for UTF-8, no idea what this actually and we don't care; only here to avoid parsing errors
#define CP_UTF8 0

// no idea what a code page looks like but we need a definition for it
typedef int dummy_codepage_t;

// some constant we use for multibyte encoding
#define MB_ERR_INVALID_CHARS 0

// also no clue; assume some number
typedef int dummy_multibyte_flags_t;

// no idea what a wide-char actually looks like but it must be wider than a byte
#define WCHAR long int

// some constant apparently indicating some issue with file attributes
#define INVALID_FILE_ATTRIBUTES 12345

// we assign the result to a long so it probably is; we also need a wide-char path name
long GetFileAttributesW(WCHAR *path);

int MultiByteToWideChar(dummy_codepage_t codepage, dummy_multibyte_flags_t conversion_flags, char *in, int other_option, WCHAR *out, unsigned int out_length);

#endif //!TARGET_WINDOWS
#endif //XPRC_WINDOWS_STUB_H