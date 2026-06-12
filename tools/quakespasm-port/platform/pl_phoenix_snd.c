/*
 * pl_phoenix_snd.c — Phoenix audio shim for Quakespasm: replaces snd_sdl.c.
 * Silent stub for first-light (SNDDMA_Init returns false -> snd_dma.c marks sound
 * not started and skips all mixing). Real PWM/I2S audio is a later tier.
 */
#include "quakedef.h"

qboolean SNDDMA_Init(dma_t *dma)
{
	(void)dma;
	return false;
}

int SNDDMA_GetDMAPos(void)
{
	return 0;
}

void SNDDMA_Shutdown(void)
{
}

void SNDDMA_LockBuffer(void)
{
}

void SNDDMA_Submit(void)
{
}

void SNDDMA_BlockSound(void)
{
}

void SNDDMA_UnblockSound(void)
{
}
