#include "lv1_07.h"

static UINT8 g_aucAlmFlag[ALM_ID_BUTT];

void Alm_SetFlag(UINT32 ulAlmId)
{
    g_aucAlmFlag[ulAlmId] = 1;                                /* ★1 */
}

void Alm_RaiseOne(UINT32 ulAlmId)
{
    Alm_SetFlag(ulAlmId);
}
