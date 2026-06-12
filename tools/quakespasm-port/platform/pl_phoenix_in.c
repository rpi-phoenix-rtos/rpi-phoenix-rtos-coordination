/*
 * pl_phoenix_in.c — Phoenix input shim for Quakespasm: replaces in_sdl.c.
 * First-light stub (no input -> e1m1 attract mode renders). The kbd0 polling that
 * feeds Key_Event lands in IN_SendKeyEvents in a later step (/dev/kbd0 works).
 */
#include "quakedef.h"

void IN_Init(void)
{
}

void IN_Shutdown(void)
{
}

void IN_Commands(void)
{
}

void IN_Move(usercmd_t *cmd)
{
	(void)cmd;
}

void IN_SendKeyEvents(void)
{
	/* TODO(quake-input): poll /dev/kbd0, translate scancodes -> Key_Event(). */
}

void IN_UpdateInputMode(void)
{
}

void IN_ClearStates(void)
{
}

void IN_Activate(void)
{
}

void IN_Deactivate(qboolean free_cursor)
{
	(void)free_cursor;
}

void IN_MouseMotion(int dx, int dy)
{
	(void)dx;
	(void)dy;
}
