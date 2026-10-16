/* max-blocks.test 的输入（SPEC-0037）：12 个 case 的 switch，最后一个 case 越界写。 */
typedef unsigned int UINT32;
typedef unsigned char UINT8;

static void Msg_Dispatch(UINT8 *buf, UINT32 type)
{
    switch (type) {
        case 0: buf[0] = 1; break;
        case 1: buf[1] = 1; break;
        case 2: buf[2] = 1; break;
        case 3: buf[3] = 1; break;
        case 4: buf[4] = 1; break;
        case 5: buf[5] = 1; break;
        case 6: buf[6] = 1; break;
        case 7: buf[7] = 1; break;
        case 8: buf[8] = 1; break;
        case 9: buf[9] = 1; break;
        case 10: buf[10] = 1; break;
        default: buf[16] = 1; break;
    }
}

void Msg_OnRecv_TP(UINT32 type)
{
    UINT8 aucBuf[16];
    Msg_Dispatch(aucBuf, type);
}
