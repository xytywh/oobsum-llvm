#include "lv1_07.h"

void Alm_Dispatch(UINT32 ulAlmId, UINT32 ulCellId)
{
    LOG_Print(2, "alarm %u on cell %u", ulAlmId, ulCellId);
    Alm_RaiseOne(ulAlmId);
}
