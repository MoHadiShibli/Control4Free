/* Host-only page fixtures use the real diagnostic renderer, without native
 * startup, filesystem access or report writes. Unused paths are discarded by
 * the art tool's section garbage collection. */
#include "../launcher/diag.c"

void c4fDiagPreviewPage(int page, const char *text)
{
    if (page < 0 || page >= C4F_DIAG_PAGES) return;
    char *copy = strdup(text ? text : "");
    if (!copy) return;
    pthread_mutex_lock(&c4fLock);
    free(c4fPages[page]);
    c4fPages[page] = copy;
    c4fGeneration++;
    pthread_mutex_unlock(&c4fLock);
}
