#!/bin/sh

# Checks for the APIs that are banned by microsoft. Since we compile for Linux
# we use the replacements from https://github.com/intel/safestringlib

set -eu

files=$(find src -iname '*.[ch]' | git check-attr --stdin citus-style | grep -v ': unset$' | sed 's/: citus-style: set$//')

# grep is allowed to fail, that means no banned matches are found
set +e
# Required banned. These functions are not allowed to be used at all.
# shellcheck disable=SC2086
grep -E '\b(strcpy|strcpyA|strcpyW|wcscpy|_tcscpy|_mbscpy|StrCpy|StrCpyA|StrCpyW|lstrcpy|lstrcpyA|lstrcpyW|_tccpy|_mbccpy|_ftcscpy|strcat|strcatA|strcatW|wcscat|_tcscat|_mbscat|StrCat|StrCatA|StrCatW|lstrcat|lstrcatA|lstrcatW|StrCatBuff|StrCatBuffA|StrCatBuffW|StrCatChainW|_tccat|_mbccat|_ftcscat|sprintfW|sprintfA|wsprintf|wsprintfW|wsprintfA|sprintf|swprintf|_stprintf|wvsprintf|wvsprintfA|wvsprintfW|vsprintf|_vstprintf|vswprintf|strncpy|wcsncpy|_tcsncpy|_mbsncpy|_mbsnbcpy|StrCpyN|StrCpyNA|StrCpyNW|StrNCpy|strcpynA|StrNCpyA|StrNCpyW|lstrcpyn|lstrcpynA|lstrcpynW|strncat|wcsncat|_tcsncat|_mbsncat|_mbsnbcat|StrCatN|StrCatNA|StrCatNW|StrNCat|StrNCatA|StrNCatW|lstrncat|lstrcatnA|lstrcatnW|lstrcatn|gets|_getts|_gettws|IsBadWritePtr|IsBadHugeWritePtr|IsBadReadPtr|IsBadHugeReadPtr|IsBadCodePtr|IsBadStringPtr|memcpy|RtlCopyMemory|CopyMemory|wmemcpy|lstrlen)\(' $files \
    && echo "ERROR: Required banned API usage detected" && exit 1

# Recommended banned. If you can change the code not to use these that would be
# great. If you REALLY need to use it you can remove from this regex and add it
# to the list below, so it stops complaining.
# Ignored from recommended banned:
# - alloca
# shellcheck disable=SC2086
grep -E '\b(wnsprintf|wnsprintfA|wnsprintfW|_snwprintf|_snprintf|_sntprintf|_vsnprintf|vsnprintf|_vsnwprintf|_vsntprintf|wvnsprintf|wvnsprintfA|wvnsprintfW|strtok|_tcstok|wcstok|_mbstok|makepath|_tmakepath| _makepath|_wmakepath|_splitpath|_tsplitpath|_wsplitpath|scanf|wscanf|_tscanf|sscanf|swscanf|_stscanf|snscanf|snwscanf|_sntscanf|_itoa|_itow|_i64toa|_i64tow|_ui64toa|_ui64tot|_ui64tow|_ultoa|_ultot|_ultow|CharToOem|CharToOemA|CharToOemW|OemToChar|OemToCharA|OemToCharW|CharToOemBuffA|CharToOemBuffW|_alloca|ChangeWindowMessageFilter)\(' $files  \
    && echo "ERROR: Recomended banned API usage detected" && exit 1
exit 0
