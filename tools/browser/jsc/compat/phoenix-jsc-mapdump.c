/*
 * Phoenix-RTOS browser track C (JavaScriptCore) -- footprint triage aid, linked into jsc only.
 *
 * Phoenix populates anonymous memory when it is mapped, so every reservation a runtime makes is
 * resident, and `--footprint` (anonymous pages of all map entries) reports their sum. To see what
 * it is made of: run jsc with PHX_MAPDUMP=1 in the environment (psh: `export PHX_MAPDUMP=1`).
 * At exit it prints one line per map entry of at least 1 MiB and a summary:
 *
 *   PHX-MAP vaddr=0x... size-kb=<n> anon-kb=<n> prot=rwx orig=rwx obj=anon|mem|oid
 *   PHX-MAP small entries=<n> anon-kb=<n>        (all entries below 1 MiB together)
 *   PHX-MAP total entries=<n> anon-kb=<n> exec-kb=<n> stack-sized=<n> (entries of 1-1.1 MiB: thread stacks)
 *
 * PHX_MAPDUMP=2 also prints mimalloc's arena list (mi_debug_show_arenas: each arena's size and
 * which of its 64 KiB slices hold pages). Silent and inert without PHX_MAPDUMP.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>


/* Weak: the build's helper programs (LLIntSettingsExtractor) link the compat object without mimalloc */
__attribute__((weak)) void mi_debug_show_arenas(void);


static int phx_mapDumpLevel;


static void phx_protStr(char out[4], unsigned char prot)
{
	out[0] = ((prot & PROT_READ) != 0) ? 'r' : '-';
	out[1] = ((prot & PROT_WRITE) != 0) ? 'w' : '-';
	out[2] = ((prot & PROT_EXEC) != 0) ? 'x' : '-';
	out[3] = '\0';
}


static void phx_mapDump(void)
{
	static const char *const objName[] = { "anon", "mem", "oid" };
	meminfo_t info;
	entryinfo_t *map = NULL, *grown;
	int mapsz = 256, i;
	unsigned long smallN = 0, smallKb = 0, totalKb = 0, execKb = 0, stacks = 0;
	char p[4], o[4];

	for (;;) {
		grown = realloc(map, (size_t)mapsz * sizeof(*map));
		if (grown == NULL) {
			free(map);
			return;
		}
		map = grown;
		memset(&info, 0, sizeof(info));
		info.page.mapsz = -1;
		info.maps.mapsz = -1;
		info.entry.kmapsz = -1;
		info.entry.pid = (unsigned int)getpid();
		info.entry.mapsz = mapsz;
		info.entry.map = map;
		meminfo(&info);
		if (info.entry.mapsz < 0) {
			free(map);
			return;
		}
		if (info.entry.mapsz <= mapsz) {
			break;
		}
		mapsz = info.entry.mapsz + 64;
	}

	for (i = 0; i < info.entry.mapsz; i++) {
		const entryinfo_t *e = &map[i];
		unsigned long anonKb = ((e->anonsz == (size_t)~0U) || (e->anonsz == (size_t)-1)) ? 0UL : (unsigned long)(e->anonsz / 1024U);
		unsigned long sizeKb = (unsigned long)(e->size / 1024U);

		totalKb += anonKb;
		if ((e->prot & PROT_EXEC) != 0) {
			execKb += anonKb;
		}
		if ((sizeKb >= 1024UL) && (sizeKb <= 1024UL + 128UL)) {
			stacks++;
		}
		if (sizeKb < 1024UL) {
			smallN++;
			smallKb += anonKb;
			continue;
		}
		phx_protStr(p, e->prot);
		phx_protStr(o, e->protOrig);
		fprintf(stderr, "PHX-MAP vaddr=%p size-kb=%lu anon-kb=%lu prot=%s orig=%s obj=%s\n", e->vaddr, sizeKb, anonKb, p, o,
				((unsigned int)e->object < 3U) ? objName[e->object] : "?");
	}
	fprintf(stderr, "PHX-MAP small entries=%lu anon-kb=%lu\n", smallN, smallKb);
	fprintf(stderr, "PHX-MAP total entries=%d anon-kb=%lu exec-kb=%lu stack-sized=%lu\n", info.entry.mapsz, totalKb, execKb, stacks);
	free(map);

	if ((phx_mapDumpLevel >= 2) && (mi_debug_show_arenas != NULL)) {
		mi_debug_show_arenas();
	}
}


__attribute__((constructor)) static void phx_mapDumpInit(void)
{
	const char *on = getenv("PHX_MAPDUMP");

	if ((on == NULL) || (on[0] < '1') || (on[0] > '9')) {
		return;
	}
	phx_mapDumpLevel = on[0] - '0';
	(void)atexit(phx_mapDump);
}
