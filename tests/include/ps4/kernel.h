#include <stdint.h>
#include <sys/types.h>
uint64_t kernel_get_ucred_authid(pid_t);
int kernel_set_ucred_authid(pid_t, uint64_t);
