/***
 * banned.h - list of Microsoft Security Development Lifecycle (SDL) banned APIs
 *
 * Purpose:
 *       This include file contains a list of banned APIs which should not be used in new code and
 *       removed from legacy code over time.
 *
 * History
 * 01-Jan-2006 - mikehow - Initial Version
 * 22-Apr-2008 - mikehow	- Updated to SDL 4.1, commented out recommendations and added memcpy
 * 26-Jan-2009 - mikehow - Updated to SDL 5.0, made the list sane, added SDL compliance levels
 * 10-Feb-2009 - mikehow - Updated based on feedback from MS Office
 * 12-May-2009 - jpardue - Added wmemcpy
 * 08-Jul-2009 - mikehow - Fixed header #ifndef/#endif logic, made the SDL recommended compliance level name more obvious
 * 05-Nov-2009 - mikehow	- Added vsnprintf (ANSI version of _vsnprintf)
 * 01-Jan-2010 - mikehow - Added better strsafe integration, now the following works:
 *							#include "strsafe.h"
 *							#include "banned.h"
 * 04-Jun-2010 - mikehow - Small "#if" bug fix
 * 16-Jun-2011 - mikehow	- Added the two _CRT_SECURE_xxxxx macros
 * 07-Jul-2011 - mikehow - Bugfix when using recommended banned functions and StrSafe. Locally surpressed C4005 warnings
 * 15-Jun-2012 - bryans  - Moved lstrlen to required banned; removed strlen, wcslen, _mbslen, _mbstrlen, StrLen from recommended banned
 *
 *
 ***/
#ifndef BANNED_H
#define BANNED_H

#define BANNED(funcname) funcname ## _is_banned_by_banned_h
#undef strcpy
#define strcpy BANNED(strcpy)

/*strcpyA, strcpyW, wcscpy, _tcscpy, _mbscpy, StrCpy, StrCpyA, StrCpyW, lstrcpy, lstrcpyA, lstrcpyW, _tccpy, _mbccpy, _ftcscpy, strcat, strcatA, strcatW, wcscat, _tcscat, _mbscat, StrCat, StrCatA, StrCatW, lstrcat, lstrcatA, lstrcatW, StrCatBuff, StrCatBuffA, StrCatBuffW, StrCatChainW, _tccat, _mbccat, _ftcscat, sprintfW, sprintfA, wsprintf, wsprintfW, wsprintfA, sprintf, swprintf, _stprintf, wvsprintf, wvsprintfA, wvsprintfW, vsprintf, _vstprintf, vswprintf, strncpy, wcsncpy, _tcsncpy, _mbsncpy, _mbsnbcpy, StrCpyN, StrCpyNA, StrCpyNW, StrNCpy, strcpynA, StrNCpyA, StrNCpyW, lstrcpyn, lstrcpynA, lstrcpynW, strncat, wcsncat, _tcsncat, _mbsncat, _mbsnbcat, StrCatN, StrCatNA, StrCatNW, StrNCat, StrNCatA, StrNCatW, lstrncat, lstrcatnA, lstrcatnW, lstrcatn, gets, _getts, _gettws, IsBadWritePtr, IsBadHugeWritePtr, IsBadReadPtr, IsBadHugeReadPtr, IsBadCodePtr, IsBadStringPtr, memcpy, RtlCopyMemory, CopyMemory, wmemcpy, lstrlen, wnsprintf, wnsprintfA, wnsprintfW, _snwprintf, _snprintf, _sntprintf, _vsnprintf, vsnprintf, _vsnwprintf, _vsntprintf, wvnsprintf, wvnsprintfA, wvnsprintfW, strtok, _tcstok, wcstok, _mbstok, makepath, _tmakepath,  _makepath, _wmakepath, _splitpath, _tsplitpath, _wsplitpath, scanf, wscanf, _tscanf, sscanf, swscanf, _stscanf, snscanf, snwscanf, _sntscanf, _itoa, _itow, _i64toa, _i64tow, _ui64toa, _ui64tot, _ui64tow, _ultoa, _ultot, _ultow, CharToOem, CharToOemA, CharToOemW, OemToChar, OemToCharA, OemToCharW, CharToOemBuffA, CharToOemBuffW, alloca, _alloca, ChangeWindowMessageFilter */

#endif /* BANNED_H */
