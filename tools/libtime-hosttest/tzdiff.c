/*
 * tzdiff.c -- differential harness for libphoenix's POSIX TZ support.
 *
 * Compiles libphoenix's real time/time.c natively and compares tzset(),
 * localtime_r(), mktime(), ctime_r() and strftime() against the host glibc,
 * for many TZ strings, with glibc's zoneinfo lookup disabled (TZDIR points to
 * a directory that does not exist), so glibc parses TZ as a POSIX string --
 * the only thing libphoenix can do.
 *
 * The transition instants are found from GLIBC alone (a scan for tm_isdst
 * flips, then a bisection to the exact second), so the reference does not
 * depend on the code under test. Every transition is probed at -1/0/+1 s and
 * at wall-clock times across the skipped and the repeated hour.
 *
 * Divergences that are understood and deliberate are classified, counted and
 * printed as KNOWN; everything else is a diff and fails the run:
 *   - mktime(tm_isdst = -1) in the repeated hour: glibc's answer depends on the
 *     offset its previous call cached, so it has no fixed answer. libphoenix
 *     returns the earlier instant; the harness checks that glibc returned one
 *     of the two valid instants and libphoenix the earlier one.
 *   - Zone names of a TZ that has no valid standard time: glibc keeps the
 *     leading letters ("Europe" from Europe/Warsaw) and names the empty TZ
 *     "Universal"; libphoenix uses "UTC" unless the whole value is a valid
 *     zone name. Checked against libphoenix's documented expectation instead.
 *   - A TZ beginning with ':': glibc parses the rest as a POSIX string when no
 *     file exists, libphoenix falls back to UTC as documented.
 *   - A malformed DST part: glibc keeps half-parsed rules, libphoenix keeps
 *     standard time only. Checked against the documented expectation.
 * DST zones are compared from 1970 on: for earlier years glibc computes the
 * changes of 1970 (tzset.c compute_change() starts from t = 0).
 *
 * A canary proves the comparison machinery reports a difference.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char *ph_tzname[2];
extern long ph_timezone;
extern int ph_daylight;
void ph_tzset(void);
struct tm *ph_localtime_r(const time_t *, struct tm *);
struct tm *ph_gmtime_r(const time_t *, struct tm *);
time_t ph_mktime(struct tm *);
char *ph_ctime_r(const time_t *, char *);
size_t ph_strftime(char *, size_t, const char *, const struct tm *);
void ph__time_init(void) __attribute__((weak));

static unsigned long total, diffs, known_overlap, known_names;
static const char *curtz;

#define MAXPRINT 60

/* diffs per kind, for the summary: a capped list understates scope */
static struct {
	const char *what;
	unsigned long n;
} kinds[16];

static void report(const char *what, long long t, const char *detail)
{
	int k;

	for (k = 0; k < 16; k++) {
		if (kinds[k].what == NULL || strcmp(kinds[k].what, what) == 0) {
			kinds[k].what = what;
			kinds[k].n++;
			break;
		}
	}
	diffs++;
	if (diffs <= MAXPRINT) {
		printf("TZDIFF TZ=%-36s %-12s t=%-12lld %s\n", curtz, what, t, detail);
	}
}

static void check(int ok, const char *what, long long t, const char *fmt, ...)
	__attribute__((format(printf, 4, 5)));

#include <stdarg.h>
static void check(int ok, const char *what, long long t, const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	total++;
	if (ok) {
		return;
	}
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	report(what, t, buf);
}

static int tm_equal(const struct tm *a, const struct tm *b)
{
	return a->tm_sec == b->tm_sec && a->tm_min == b->tm_min && a->tm_hour == b->tm_hour &&
		a->tm_mday == b->tm_mday && a->tm_mon == b->tm_mon && a->tm_year == b->tm_year &&
		a->tm_wday == b->tm_wday && a->tm_yday == b->tm_yday && a->tm_isdst == b->tm_isdst;
}

static const char *tm_str(const struct tm *t, char *buf)
{
	sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d wd%d yd%d dst%d", t->tm_year + 1900, t->tm_mon + 1,
		t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, t->tm_wday, t->tm_yday, t->tm_isdst);
	return buf;
}

/* ---- per-TZ expectations ------------------------------------------------ */

enum { cls_posix, cls_fallback, cls_colon, cls_malformed };

struct tzcase {
	const char *tz;
	int cls;
	/* expected libphoenix values for the classes glibc cannot judge */
	const char *name0, *name1;
	long timezone;
	int daylight;
};

static const struct tzcase CASES[] = {
	/* DST zones, northern hemisphere */
	{ "CET-1CEST,M3.5.0,M10.5.0/3", cls_posix },
	{ "EET-2EEST,M3.5.0/3,M10.5.0/4", cls_posix },
	{ "EST5EDT,M3.2.0,M11.1.0", cls_posix },
	{ "EST5EDT", cls_posix }, /* rules omitted: US default */
	{ "PST8PDT,M3.2.0/2:00:00,M11.1.0/2:00:00", cls_posix },
	{ "AKST9AKDT,M3.2.0,M11.1.0", cls_posix },
	{ "HST10HDT,M3.2.0,M11.1.0", cls_posix },
	{ "<-02>2<-01>,M3.5.0/-1,M10.5.0/0", cls_posix },       /* America/Nuuk: negative time */
	{ "<-03>3<-02>,M3.5.0/-2,M10.5.0/-1", cls_posix },
	{ "EET-2EEST,M2.5.4/24,M10.5.5/1", cls_posix },          /* last Thursday of February 24:00 */
	{ "MMM-1NNN-2,M2.5.1/25,M11.5.6/-3", cls_posix },        /* week 5 of February, time > 24h */
	{ "<+0330>-3:30<+0430>,J79/24,J263/24", cls_posix },    /* old Asia/Tehran: J rules, 24:00 */
	{ "AAA+5BBB,J60/2,J300/2", cls_posix },                  /* J60 is always March 1st */
	{ "AAA+5BBB,59/2,300/2", cls_posix },                    /* n: day 59 is Feb 29 in leap years */
	{ "AAA+5BBB,0/0,365/0", cls_posix },                     /* n: day 365 is Jan 1 in common years */
	{ "AAA3BBB,M1.1.0,M12.5.6", cls_posix },                 /* changes near the year boundary */
	{ "<+00>0<+02>-2,M3.5.0/1,M10.5.0/3", cls_posix },     /* Antarctica/Troll: 2 h DST */
	{ "XXX-1YYY-3:30,M4.1.3/1:23:45,M9.3.2/0:59:59", cls_posix },
	{ "WART4WARST,J1/0,J365/25", cls_posix },                /* permanent DST idiom */
	/* southern hemisphere: DST spans the new year */
	{ "AEST-10AEDT,M10.1.0,M4.1.0/3", cls_posix },
	{ "ACST-9:30ACDT,M10.1.0,M4.1.0/3", cls_posix },
	{ "NZST-12NZDT,M9.5.0,M4.1.0/3", cls_posix },
	{ "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45", cls_posix }, /* Pacific/Chatham */
	{ "LHST-10:30LHDT-11,M10.1.0,M4.1.0", cls_posix },      /* Lord Howe: 30 min DST */
	{ "<-04>4<-03>,M9.1.6/24,M4.1.6/24", cls_posix },       /* America/Santiago */
	{ "IST-1GMT0,M10.5.0,M3.5.0/1", cls_posix },            /* Europe/Dublin: negative DST */
	/* no DST */
	{ "UTC0", cls_posix },
	{ "GMT0", cls_posix },
	{ "EST5", cls_posix },
	{ "JST-9", cls_posix },
	{ "IST-5:30", cls_posix },
	{ "NPT-5:45", cls_posix },
	{ "<+0330>-3:30", cls_posix },
	{ "<-03>3", cls_posix },
	{ "<+14>-14", cls_posix },
	{ "<-12>+12", cls_posix },
	{ "AAA+12:34:56", cls_posix },
	{ "XXX-24", cls_posix },
	{ "YYY24", cls_posix },
	{ "abcdefghijklmno-1", cls_posix },                      /* 15 letters, the longest name kept */
	/* no valid standard time: UTC */
	{ "", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "Europe/Warsaw", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "America/New_York", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "UTC", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "GMT", cls_fallback, "GMT", "GMT", 0, 0 },
	{ "CET", cls_fallback, "CET", "CET", 0, 0 },
	{ "<+05>", cls_fallback, "+05", "+05", 0, 0 },
	{ "garbage!", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "AB0", cls_fallback, "UTC", "UTC", 0, 0 },           /* name too short */
	{ "<AB>0", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "CET+", cls_fallback, "UTC", "UTC", 0, 0 },
	{ "abcdefghijklmnop-1", cls_fallback, "UTC", "UTC", 0, 0 }, /* 16 letters: too long */
	{ ":Europe/Warsaw", cls_colon, "UTC", "UTC", 0, 0 },
	{ ":UTC", cls_colon, "UTC", "UTC", 0, 0 },
	{ ":", cls_colon, "UTC", "UTC", 0, 0 },
	{ ":EST5EDT", cls_colon, "UTC", "UTC", 0, 0 },
	{ ":CET-1CEST,M3.5.0,M10.5.0/3", cls_colon, "UTC", "UTC", 0, 0 },
	/* malformed DST part: standard time only */
	{ "EST5EDT,M3.2.0", cls_malformed, "EST", "EST", 18000, 0 },
	{ "CET-1CEST,M3.5.0,M10.5.0/3,", cls_malformed, "CET", "CET", -3600, 0 },
	{ "CET-1CEST,M13.5.0,M10.5.0", cls_malformed, "CET", "CET", -3600, 0 },
	{ "CET-1CEST,M3.6.0,M10.5.0", cls_malformed, "CET", "CET", -3600, 0 },
	{ "CET-1CEST,M3.5.7,M10.5.0", cls_malformed, "CET", "CET", -3600, 0 },
	{ "CET-1CEST,J0,J100", cls_malformed, "CET", "CET", -3600, 0 },
	{ "CET-1CEST,366,100", cls_malformed, "CET", "CET", -3600, 0 },
	{ "CET-1CEST,M3.5.0/168,M10.5.0", cls_malformed, "CET", "CET", -3600, 0 },
	{ "JST-9JDT-25", cls_malformed, "JST", "JST", -32400, 0 },
	{ "JST-9!", cls_malformed, "JST", "JST", -32400, 0 },
};

/* ---- the comparisons ---------------------------------------------------- */

static int cls;

static void set_tz(const char *tz)
{
	if (tz == NULL) {
		unsetenv("TZ");
	}
	else {
		setenv("TZ", tz, 1);
	}
	tzset();
	ph_tzset();
}

/* The reference: glibc under the same TZ, or for the classes glibc parses
 * differently the fixed offset libphoenix documents for them, built from
 * glibc's TZ-independent gmtime_r() and timegm(). */
static long fixed_off; /* seconds EAST, cls != cls_posix */

static int ref_localtime(time_t t, struct tm *res)
{
	time_t l = t + fixed_off;

	if (cls == cls_posix) {
		return localtime_r(&t, res) != NULL;
	}
	if (gmtime_r(&l, res) == NULL) {
		return 0;
	}
	res->tm_isdst = 0;
	return 1;
}

static time_t ref_mktime(struct tm *tm)
{
	int isdst = tm->tm_isdst;
	time_t t;

	if (cls == cls_posix) {
		return mktime(tm);
	}
	/* glibc presumes a one hour DST when asked for one in a zone without it */
	t = timegm(tm) - fixed_off - ((isdst > 0) ? 3600 : 0);
	ref_localtime(t, tm);
	return t;
}

static void check_localtime(time_t t)
{
	struct tm a, b;
	char pa[128], gb[128], s1[64], s2[64];
	static const char *const FMTS[] = { "%Z %z", "%c %Z", "%s", "%H:%M:%S %z" };
	int i;

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	if (!ref_localtime(t, &b)) {
		return;
	}
	check(ph_localtime_r(&t, &a) != NULL, "localtime_r", t, "returned NULL, glibc %s", tm_str(&b, s2));
	check(tm_equal(&a, &b), "localtime_r", t, "ph=%s glibc=%s", tm_str(&a, s1), tm_str(&b, s2));

	for (i = 0; i < (int)(sizeof(FMTS) / sizeof(FMTS[0])); i++) {
		if (cls != cls_posix) {
			break; /* glibc runs a different TZ; names and %z judged in check_names() */
		}
		ph_strftime(pa, sizeof(pa), FMTS[i], &a);
		strftime(gb, sizeof(gb), FMTS[i], &b);
		check(strcmp(pa, gb) == 0, "strftime", t, "fmt=\"%s\" ph=\"%s\" glibc=\"%s\"", FMTS[i], pa, gb);
	}

	if (cls == cls_posix) {
		ph_ctime_r(&t, pa);
		ctime_r(&t, gb);
		check(strcmp(pa, gb) == 0, "ctime_r", t, "ph=\"%.24s\" glibc=\"%.24s\"", pa, gb);
	}

	/* closed identity: mktime(localtime(t)) == t with the tm_isdst localtime gave */
	{
		struct tm c = a;
		time_t back = ph_mktime(&c);
		check(back == t, "roundtrip", t, "mktime(localtime_r(t)) = %lld", (long long)back);
	}
}

static unsigned long known_glibc_wrong;

static int same_wall(const struct tm *a, const struct tm *b)
{
	return a->tm_sec == b->tm_sec && a->tm_min == b->tm_min && a->tm_hour == b->tm_hour &&
		a->tm_mday == b->tm_mday && a->tm_mon == b->tm_mon && a->tm_year == b->tm_year;
}

/* mktime() of a wall clock time, all three tm_isdst values */
static void check_mktime(const struct tm *wall)
{
	static const int ISDST[] = { -1, 0, 1 };
	int i;

	for (i = 0; i < 3; i++) {
		struct tm a = *wall, b = *wall;
		time_t ta, tb;
		char s1[64], s2[64], s3[64];

		a.tm_isdst = b.tm_isdst = ISDST[i];
		tb = ref_mktime(&b);
		ta = ph_mktime(&a);

		if ((ta == tb) && tm_equal(&a, &b)) {
			total++;
			continue;
		}

		if ((ISDST[i] < 0) && (cls == cls_posix)) {
			/* glibc's answer in the repeated hour depends on its cached offset.
			 * Accept it only if both answers are valid readings of that wall
			 * clock time and libphoenix's is the earlier one. */
			struct tm c = *wall, d = *wall, ra, rb;
			time_t t0, t1, lo, hi;
			c.tm_isdst = 0;
			d.tm_isdst = 1;
			t0 = mktime(&c);
			t1 = mktime(&d);
			lo = (t0 < t1) ? t0 : t1;
			hi = (t0 < t1) ? t1 : t0;
			localtime_r(&lo, &ra);
			localtime_r(&hi, &rb);
			if ((lo != hi) && (ta == lo) && ((tb == lo) || (tb == hi)) &&
					(ra.tm_hour == rb.tm_hour) && (ra.tm_min == rb.tm_min) && (ra.tm_sec == rb.tm_sec) &&
					(ra.tm_mday == rb.tm_mday)) {
				total++;
				known_overlap++;
				continue;
			}
		}

		if ((ISDST[i] < 0) && (cls == cls_posix)) {
			/* glibc can answer with an instant that is not a reading of the
			 * requested wall clock time at all (seen with the permanent DST
			 * idiom J1/0,J365/25 at the year boundary). Accept that only when
			 * libphoenix's answer is such a reading by GLIBC's localtime_r(),
			 * so that the excuse does not rest on the code under test. */
			struct tm n = *wall, g;
			time_t u = timegm(&n);
			gmtime_r(&u, &n);
			localtime_r(&ta, &g);
			if (same_wall(&g, &n) && same_wall(&a, &n) && !same_wall(&b, &n)) {
				total++;
				known_glibc_wrong++;
				if (known_glibc_wrong <= 5) {
					printf("TZ-HOST KNOWN glibc mktime answer is not a reading of the wall time: TZ=%s in=%s glibc=%s ph=%s\n",
						curtz, tm_str(&n, s3), tm_str(&b, s2), tm_str(&a, s1));
				}
				continue;
			}
		}

		check(0, "mktime", (long long)tb, "in=%s isdst=%d ph=%lld %s glibc=%lld %s", tm_str(wall, s3), ISDST[i],
			(long long)ta, tm_str(&a, s1), (long long)tb, tm_str(&b, s2));
	}
}

/* Wall clock readings around a change, as seen from both offsets */
static void check_change(time_t t)
{
	static const int DELTA[] = { -5400, -3601, -3600, -3599, -1801, -1800, -1, 0, 1, 1799, 1800,
		3599, 3600, 3601, 5400, 7199, 7200, 7201 };
	struct tm before, after;
	time_t p = t - 1;
	int i, k;

	check_localtime(t - 1);
	check_localtime(t);
	check_localtime(t + 1);

	localtime_r(&p, &before);
	localtime_r(&t, &after);

	for (k = 0; k < 2; k++) {
		long off = (k == 0) ? before.tm_gmtoff : after.tm_gmtoff;
		for (i = 0; i < (int)(sizeof(DELTA) / sizeof(DELTA[0])); i++) {
			time_t w = t + off + DELTA[i];
			struct tm wall;
			ph_gmtime_r(&w, &wall); /* same as glibc's, checked by tdiff */
			wall.tm_wday = wall.tm_yday = -1;
			check_mktime(&wall);
		}
	}
}

/* Find every tm_isdst flip glibc sees in [from, to) */
static unsigned long find_changes(time_t from, time_t to)
{
	const time_t step = 6 * 3600;
	struct tm tm;
	time_t t, lo, hi, mid;
	int prev, cur;
	unsigned long n = 0;

	localtime_r(&from, &tm);
	prev = tm.tm_isdst;
	for (t = from + step; t < to; t += step) {
		localtime_r(&t, &tm);
		cur = tm.tm_isdst;
		if (cur != prev) {
			lo = t - step;
			hi = t;
			while (hi - lo > 1) {
				mid = lo + (hi - lo) / 2;
				localtime_r(&mid, &tm);
				if (tm.tm_isdst == prev) {
					lo = mid;
				}
				else {
					hi = mid;
				}
			}
			check_change(hi);
			n++;
			prev = cur;
		}
	}
	return n;
}

static void check_names(const struct tzcase *c)
{
	if (c->cls == cls_posix) {
		check(strcmp(ph_tzname[0], tzname[0]) == 0, "tzname[0]", 0, "ph=\"%s\" glibc=\"%s\"", ph_tzname[0], tzname[0]);
		check(strcmp(ph_tzname[1], tzname[1]) == 0, "tzname[1]", 0, "ph=\"%s\" glibc=\"%s\"", ph_tzname[1], tzname[1]);
		check(ph_timezone == timezone, "timezone", 0, "ph=%ld glibc=%ld", ph_timezone, timezone);
		check(ph_daylight == daylight, "daylight", 0, "ph=%d glibc=%d", ph_daylight, daylight);
		return;
	}

	/* documented libphoenix behaviour; glibc's differs for these */
	check(strcmp(ph_tzname[0], c->name0) == 0, "tzname[0]", 0, "ph=\"%s\" expected=\"%s\"", ph_tzname[0], c->name0);
	check(strcmp(ph_tzname[1], c->name1) == 0, "tzname[1]", 0, "ph=\"%s\" expected=\"%s\"", ph_tzname[1], c->name1);
	check(ph_timezone == c->timezone, "timezone", 0, "ph=%ld expected=%ld", ph_timezone, c->timezone);
	check(ph_daylight == c->daylight, "daylight", 0, "ph=%d expected=%d", ph_daylight, c->daylight);

	/* %Z and %z of a local time */
	{
		time_t t = 1790000000; /* 2026-09-21 */
		struct tm a;
		char buf[64], exp[64];
		long off = -c->timezone;
		ph_localtime_r(&t, &a);
		ph_strftime(buf, sizeof(buf), "%Z %z", &a);
		snprintf(exp, sizeof(exp), "%s %c%02ld%02ld", c->name0, off < 0 ? '-' : '+', labs(off) / 3600, labs(off) / 60 % 60);
		check(strcmp(buf, exp) == 0, "strftime", t, "fmt=\"%%Z %%z\" ph=\"%s\" expected=\"%s\"", buf, exp);
	}
	if (c->cls != cls_posix && strcmp(tzname[0], c->name0) != 0) {
		known_names++;
	}
}

static unsigned long long rng = 0x9E3779B97F4A7C15uLL;

static unsigned long long next(void)
{
	rng ^= rng >> 12;
	rng ^= rng << 25;
	rng ^= rng >> 27;
	return rng * 0x2545F4914F6CDD1DuLL;
}

static void run_case(const struct tzcase *c)
{
	unsigned long changes = 0;
	int hasdst, i;
	time_t t;

	curtz = c->tz;
	cls = c->cls;
	set_tz(c->tz);
	check_names(c);

	fixed_off = -c->timezone;
	hasdst = (c->cls == cls_posix) && (daylight != 0);

	if (hasdst) {
		changes = find_changes(0, (time_t)4102444800LL); /* 1970 .. 2100 */
		check(changes >= 2 * 100, "changes", 0, "only %lu changes found", changes);
	}

	/* random instants: 1970..2400 for DST zones, 1600..2400 otherwise */
	for (i = 0; i < 20000; i++) {
		unsigned long long r = next();
		if (hasdst) {
			t = (time_t)(r % 13569465600uLL);
		}
		else {
			t = (time_t)(r % 25000000000uLL) - 11000000000LL;
		}
		check_localtime(t);
	}

	/* random wall clock times, fields out of range included */
	for (i = 0; i < 20000; i++) {
		struct tm w;
		memset(&w, 0, sizeof(w));
		/* from 1971, so that out of range fields cannot normalise into 1969 */
		w.tm_year = 71 + (int)(next() % 130);
		w.tm_mon = (int)(next() % 14) - 1;
		w.tm_mday = (int)(next() % 33);
		w.tm_hour = (int)(next() % 27) - 1;
		w.tm_min = (int)(next() % 62) - 1;
		w.tm_sec = (int)(next() % 61);
		check_mktime(&w);
	}

	if (hasdst) {
		printf("TZ-HOST %-44s changes=%lu\n", c->tz, changes);
	}
}

int main(void)
{
	int i;

	setenv("TZDIR", "/nonexistent-tzdir", 1);

	if (ph__time_init != NULL) {
		ph__time_init();
	}

	curtz = "(canary)";
	{
		unsigned long before = diffs;
		report("canary", 0, "must fire");
		if (diffs != before + 1) {
			printf("TZ-HOST BROKEN canary-did-not-fire\n");
			return 2;
		}
		diffs = before;
		printf("TZ-HOST canary ok\n");
	}

	/* before any tzset(): the names must already be usable */
	curtz = "(before tzset)";
	check(ph_tzname[0] != NULL && ph_tzname[1] != NULL && strcmp(ph_tzname[0], "UTC") == 0, "tzname", 0,
		"tzname[0] before tzset() is %s", ph_tzname[0] != NULL ? ph_tzname[0] : "NULL");

	/* unset TZ: glibc would read /etc/localtime, so judge against the documented UTC */
	curtz = "(unset)";
	unsetenv("TZ");
	ph_tzset();
	check(strcmp(ph_tzname[0], "UTC") == 0 && strcmp(ph_tzname[1], "UTC") == 0 && ph_timezone == 0 && ph_daylight == 0,
		"tzset", 0, "tzname=%s/%s timezone=%ld daylight=%d", ph_tzname[0], ph_tzname[1], ph_timezone, ph_daylight);

	/* localtime_r() and mktime() pick up a changed TZ without tzset() */
	curtz = "(no tzset)";
	{
		time_t t = 1782900000; /* 2026-07-01 10:00:00 UTC */
		struct tm a;
		setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
		ph_localtime_r(&t, &a);
		check(a.tm_hour == 12 && a.tm_isdst == 1, "localtime_r", t, "hour=%d isdst=%d, want 12 and 1", a.tm_hour, a.tm_isdst);
		setenv("TZ", "JST-9", 1);
		a.tm_isdst = -1;
		/* 12:00 JST is 03:00 UTC */
		check(ph_mktime(&a) == t - 7 * 3600, "mktime", t, "did not re-read TZ");
	}

	for (i = 0; i < (int)(sizeof(CASES) / sizeof(CASES[0])); i++) {
		run_case(&CASES[i]);
	}

	for (i = 0; i < 16 && kinds[i].what != NULL; i++) {
		if (strcmp(kinds[i].what, "canary") != 0) {
			printf("TZ-HOST diffs %-12s %lu\n", kinds[i].what, kinds[i].n);
		}
	}
	printf("TZ-HOST KNOWN repeated-hour=%lu fallback-names=%lu glibc-not-a-reading=%lu\n",
		known_overlap, known_names, known_glibc_wrong);
	printf("TZ-HOST cases=%d total=%lu diffs=%lu\n", (int)(sizeof(CASES) / sizeof(CASES[0])), total, diffs);
	return (diffs != 0) ? 1 : 0;
}
