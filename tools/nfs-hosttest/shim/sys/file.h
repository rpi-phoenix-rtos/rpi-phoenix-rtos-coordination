/* Host shim: Phoenix object types and attribute ids (libphoenix sys/file.h,
 * phoenix-rtos-kernel include/file.h). */
#ifndef NFS_HOSTTEST_SYS_FILE_H
#define NFS_HOSTTEST_SYS_FILE_H

#include <fcntl.h>
#include <sys/msg.h>

enum { otDir = 0, otFile, otDev, otSymlink, otUnknown };
enum { atMode = 0, atUid, atGid, atSize, atBlocks, atIOBlock, atType, atPort, atPollStatus, atEventMask, atCTime,
	atMTime, atATime, atLinks, atDev };

#endif
