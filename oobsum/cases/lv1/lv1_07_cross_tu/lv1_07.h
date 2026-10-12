#ifndef LV1_07_H
#define LV1_07_H
#include "../../common.h"

typedef enum {
    ALM_ID_CELL_DOWN,
    ALM_ID_LINK_BROKEN,
    ALM_ID_CLK_LOSS,
    ALM_ID_BUTT
} ALM_ID_E;

void Alm_Dispatch(UINT32 ulAlmId, UINT32 ulCellId);     /* alm_mid.c */
void Alm_SetFlag(UINT32 ulAlmId);                       /* alm_low.c */
void Alm_RaiseOne(UINT32 ulAlmId);                      /* alm_low.c */
#endif
