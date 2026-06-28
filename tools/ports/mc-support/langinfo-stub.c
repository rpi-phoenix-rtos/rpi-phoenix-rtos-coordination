/*
 * Phoenix-RTOS — stub nl_langinfo for Midnight Commander.
 *
 * Phoenix libc has no langinfo. Returns fixed C/POSIX-locale strings;
 * nl_langinfo(CODESET) returns "UTF-8" to match the identity stub libiconv so mc
 * treats the console as UTF-8. Replace with a real locale-backed implementation
 * for full localized date/number formatting.
 *
 * Copyright 2026 Phoenix Systems
 * Author: Witold Bołt
 */
#include "langinfo.h"

char *nl_langinfo(nl_item item)
{
	switch (item) {
	case CODESET:    return (char *)"UTF-8";
	case D_T_FMT:    return (char *)"%a %b %e %H:%M:%S %Y";
	case D_FMT:      return (char *)"%m/%d/%y";
	case T_FMT:      return (char *)"%H:%M:%S";
	case T_FMT_AMPM: return (char *)"%I:%M:%S %p";
	case AM_STR:     return (char *)"AM";
	case PM_STR:     return (char *)"PM";
	case RADIXCHAR:  return (char *)".";
	case THOUSEP:    return (char *)"";
	case YESEXPR:    return (char *)"^[yY]";
	case NOEXPR:     return (char *)"^[nN]";
	case CRNCYSTR:   return (char *)"";
	default:         return (char *)"";
	}
}
