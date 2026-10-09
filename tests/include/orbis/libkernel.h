#ifndef C4F_TEST_ORBIS_KERNEL_H
#define C4F_TEST_ORBIS_KERNEL_H
#include <stdint.h>
typedef struct { int Size; char VersionString[28]; uint32_t Version; } OrbisKernelSwVersion;
int sceKernelGetSystemSwVersion(OrbisKernelSwVersion *version);
#endif
