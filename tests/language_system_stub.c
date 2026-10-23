/* Host render fixtures start in English unless they explicitly choose a locale. */
int sceSystemServiceParamGetInt(int id, int *value)
{
    (void)id;
    *value = 1;
    return 0;
}
