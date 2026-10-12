# 场景用例一览（由 run.py index 生成）

| 编号 | 用例 | 场景 / 签名 | 年内 | 期望 | 文件 |
|---|---|---|---|---|---|
| LV1-01 | 统计计数：枚举结束标记 BUTT 被当成下标逐层传下去 | S1.6  A1·M2·P0·V1·R1·X0·K0·G0 | 承诺 | TP Cell_OnSetupTimeout_TP pa=L1；TN Cell_OnSetupTimeout_TN pa=- | `lv1/lv1_01_stat_enum_butt.c` |
| LV1-02 | 名字缓冲区逐层下传，最底层按"长度"位置写结尾符 | S1.1  A1·M1·P1·V0·R1·X0·K0·G0 | 承诺 | TP Ue_ShowInfo_TP pa=L1；TN Ue_ShowInfo_TN pa=- | `lv1/lv1_02_name_terminator.c` |
| LV1-03 | 常量长度的堆内存逐层下传，最底层循环多写一个 | S1.2+S1.3  A1·M4·P1·V0·R1·X0·K2·G0 | 承诺 | TP Msg_Build_TP pa=L2；TN Msg_Build_TN pa=- | `lv1/lv1_03_heap_const_loop.c` |
| LV1-04 | 结构体数组成员的地址逐层下传，底层循环用了新规格的宏 | S1.4  A1·M3·P1·V0·R1·X0·K2·G0 | 承诺 | TP Cell_Setup_TP pa=L2；TN Cell_Setup_TN pa=- | `lv1/lv1_04_member_array_macro.c` |
| LV1-05 | 结构体扩展后，清零函数按扩展后的大小 memset_s，传进来的却是老结构体 | S1.7  A3·M1·P1·V0·R1·X0·K0·G0 | 承诺 | TP Ue_OnDetach_TP pa=L1；TN Ue_OnDetach_TN pa=- | `lv1/lv1_05_memset_ext_struct.c` |
| LV1-06 | 查表函数自己的常量表，下标从多层之上传入，中间层只是改名转存 | S1.5  A1·M1·P0·V1·R1·X0·K0·G0 | 承诺 | TP Sched_InitDefault_TP pa=L1；TN Sched_InitDefault_TN pa=- | `lv1/lv1_06_qci_table_local.c` |
| LV1-07 | 跨文件：告警 ID 用了 BUTT，经三个文件三层到达标志数组 | S1.6  A1·M2·P0·V1·R1·X0·K0·G0（跨 TU） | 承诺 | TP Alm_ReportAll_TP pa=L1；TN Alm_ReportAll_TN pa=- | `lv1/lv1_07_cross_tu` |
| LV1-08 | 拷贝模板：源缓冲区逐层下传，最底层按新长度读（读越界） | S1.7  A3·M2·P1·V0·R1·X0·K0·G0 | 承诺 | TP Cfg_InitDefault_TP pa=L1；TN Cfg_InitDefault_TN pa=- | `lv1/lv1_08_tmpl_src_read.c` |
| LV2-01 | 把 sizeof(数组) 当成"个数"传下去，底层按个数循环写 UINT16 | S2.1+S2.2  A1·M1·P1·V1·R2·X1·K2·G0 | 承诺 | TP Erab_OnSetupReq_TP pa=L2；TN Erab_OnSetupReq_TN pa=- | `lv2/lv2_01_sizeof_as_count.c` |
| LV2-02 | 跳过消息头后下传，长度没扣头长（读越界） | S2.3  A3·M1·P1·V1·R2·X1·K0·G0 | 承诺 | TP Msg_Entry_TP pa=L1；TN Msg_Entry_TN pa=- | `lv2/lv2_02_skip_header_len.c` |
| LV2-03 | memcpy_s(pkt + off, 整包大小, …)：destMax 没扣偏移，偏移在中间层又加了头长 | S2.4  A3·M1·P1·V1·R2·X1·K0·G0 | 承诺 | TP Pkt_Build_TP pa=L1；TN Pkt_Build_TN pa=- | `lv2/lv2_03_tlv_destmax_no_off.c` |
| LV2-04 | 按 n 个元素申请，结束标记写在第 n 个（申请与访问同源，不需要知道 n） | S2.5  A1·M5·P1·V1·R2·X1·K0·G0 | 承诺 | TP List_Create_TP pa=L1；TN List_Create_TN pa=- | `lv2/lv2_04_heap_n_items_end.c` |
| LV2-05 | 字节型位图按 UINT32 清零，字数算多了一个 | S2.6+S2.7  A1·M1·P1·V1·R2·X2+X3·K2·G0 | 承诺 | TP Cell_ResetUeMap_TP pa=L2；TN Cell_ResetUeMap_TN pa=- | `lv2/lv2_05_bitmap_cast_words.c` |
| LV2-06 | 置位图：上层按扩容后的 UE 规格循环，下层按 idx / 32 定位字 | S2.7+S3.9  A1·M1·P1·V1·R2·X3·K2·G0 | 承诺 | TP Cell_RefreshActive_TP pa=L2；TN Cell_RefreshActive_TN pa=- | `lv2/lv2_06_bitmap_set_loop.c` |
| LV2-07 | 哈希桶下标写成 key & BUCKET_NUM（应为 BUCKET_NUM − 1） | S2.7  A1·M1·P1·V5→X3·R2·X3·K0·G0 | 承诺 | TP Tbl_OnAddReq_TP pa=L2；TN Tbl_OnAddReq_TN pa=- | `lv2/lv2_07_hash_mask_wrong.c` |
| LV2-08 | 协议栈逐层加头：每层偏移都没问题，加起来超了帧长 | S2.3  A2·M1·P1·V0·R1·X1·K0·G0 | 承诺 | TP Frame_Build_TP pa=L1；TN Frame_Build_TN pa=- | `lv2/lv2_08_layered_headers.c` |
| LV2-09 | 无符号下标减一回绕：pos 为 0 时取"前一个" | S2.2  A1·M1·P1·V1·R2·X1·K0·G0/G1 | 承诺 | TP Ue_CheckJitter_TP pa=L1；TN Ue_CheckJitter_TN pa=- | `lv2/lv2_09_unsigned_wrap.c` |
| LV3-01 | 消息入口校验写成 > MAX（应为 >=），小区号三层之后索引全局上下文表 | S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2 | 承诺 | TP Cell_OnSetupReq_TP pa=L2；TN Cell_OnSetupReq_TN pa=- | `lv3/lv3_01_check_gt_vs_ge.c` |
| LV3-02 | 校验用接入模块的规格宏，数组用上下文模块的老规格宏 | S3.1  A1·M2·P0·V5→G2·R1·X0·K1·G2 | 承诺 | TP Ue_OnAttach_TP pa=L2；TN Ue_OnAttach_TN pa=- | `lv3/lv3_02_check_wrong_macro.c` |
| LV3-03 | 访问函数自己校验了，但校验的界比多层之上传进来的数组大（TN 是常量调用点，不能误报） | S3.2  A1·M2·P1·V5→G1·R2·X1·K1·G1 | 承诺 | TP Board_OnCfgMsg_TP pa=L2；TN Board_InitDefault_TN pa=- | `lv3/lv3_03_callee_guard_wrong.c` |
| LV3-04 | 按源列表的个数循环拷贝，目的数组是老规格 | S3.4  A1·M1·P1·V5→G2·R2·X1·K2·G2 | 承诺 | TP Cell_OnNbrUpdate_TP pa=L2；TN Cell_OnNbrUpdate_TN pa=- | `lv3/lv3_04_copy_by_src_count.c` |
| LV3-05 | 填满后补结尾符：循环退出时计数器等于长度 | S3.5  A1·M1·P1·V1·R2·X0·K2·G0 | 承诺 | TP Cell_SetDefaultName_TP pa=L1；TN Cell_SetDefaultName_TN pa=- | `lv3/lv3_05_pad_then_terminate.c` |
| LV3-06 | 按制式选长度：NR 的能力长度超过了缓冲区（分支合并后一支越界） | S3.3  A3·M1·P1·V1·R2·X0·K1·G0 | 尽力 | TP Ue_OnCapInfo_TP pa=L2；TN Ue_OnCapInfo_TN pa=- | `lv3/lv3_06_rat_branch_len.c` |
| LV3-07 | 报文组装循环：按剩余长度拷贝（安全） vs. destMax 用整包长度（缺陷） | S3.7+S3.8  A3·M1·P1·V1·R2·X1·K3·G1 | 承诺（TN）/ 尽力（TP） | TP Msg_Encode_TP pa=L2；TN Msg_Encode_TN pa=- | `lv3/lv3_07_pkt_loop_destmax.c` |
| LV3-08 | 调用链在循环里：上层遍历小区用了 <=，三层之下按小区号清上下文 | S3.9  A3·M2·P0·V1·R1·X1·K2·G0 | 承诺 | TP Cell_ResetAll_TP pa=L2；TN Cell_ResetAll_TN pa=- | `lv3/lv3_08_caller_loop_le.c` |
| LV3-09 | 调度网格清零：行列宏写反（嵌套循环，各维界是常量） | S3.6  A1·M1·P1·V0·R1·X1·K2·G0 | 承诺 | TP Sched_InitGrid_TP pa=L2；TN Sched_InitGrid_TN pa=- | `lv3/lv3_09_grid_row_col_swap.c` |
| LV3-10 | 校验之后又改了下标："取下一个位置"没再校验 | S3.1  A1·M2·P0·V5→G2·R1·X1·K1·G2 | 承诺 | TP Ue_OnHoPrepare_TP pa=L2；TN Ue_OnHoPrepare_TN pa=- | `lv3/lv3_10_modify_after_check.c` |
| LV4-01 | 三层封装的分配器按老规格申请，初始化按新规格遍历 | S4.1  A1·M4·P2+P1·V0·R1·X1·K2·G0 | 承诺 | TP Ue_ModuleInit_TP pa=L2；TN Ue_ModuleInit_TN pa=- | `lv4/lv4_01_wrapper_alloc_spec.c` |
| LV4-02 | 申请写成 sizeof(指针)，三层之下按结构体成员写 | S4.1  A3·M4·P2+P1·V0·R1·X1·K0·G0 | 承诺 | TP Ue_CreateSec_TP pa=L1；TN Ue_CreateSec_TN pa=- | `lv4/lv4_02_sizeof_pointer.c` |
| LV4-03 | 带头部的缓冲池分配器：调用方按"长度 + 结尾符"写 | S4.2  A1·M5·P2+P1·V1·R2·X1·K0·G0 | 承诺 | TP Msg_MakeText_TP pa=L1；TN Msg_MakeText_TN pa=- | `lv4/lv4_03_hdr_pool_alloc.c` |
| LV4-04 | 出参分配器申请报告缓冲，填充时按"报告长 + 尾长"清零 | S4.3  A3·M4·P2+P1·V0·R1·X1·K0·G0 | 承诺 | TP Rpt_Send_TP pa=L1；TN Rpt_Send_TN pa=- | `lv4/lv4_04_outparam_alloc.c` |
| LV4-05 | 个数来自"取配置并截断"的 getter（两层），使用处循环多了一个等号 | S4.4  A1·M1·P1·V2·R3·X0·K2·G0 | 承诺 | TP Ue_OnReconfig_TP pa=L2；TN Ue_OnReconfig_TN pa=- | `lv4/lv4_05_getter_clamp_le.c` |
| LV4-06 | 校验封装在独立函数里（返回布尔），校验函数自己差了一 | S4.6（v3.11 新增）  A3·M2·P0·V5→G3·R1·X1·K1·G3 | 尽力 | TP Cell_OnDelReq_TP pa=L2；TN Cell_OnDelReq_TN pa=- | `lv4/lv4_06_validator_func.c` |
| LV4-07 | 分配器的函数体不在分析范围里，只出现在 config.ini / alloc_free_identity 结果里 | S4.5  A1·M5·P2+P1·V0·R1·X0·K0·G0 | 规则性处理 | TP Pool_UseBlock_TP pa=记账；TN Pool_UseBlock_TN pa=- | `lv4/lv4_07_opaque_allocator.c` |
| LV5-01 | 编码上下文在链顶填好（缓冲区 + 长度），长度用了新宏，缓冲区还是老大小 | S5.1  A3·M1·P3·V3·R2·X0·K0·G0 | 承诺 | TP Rrc_EncodeMsg_TP pa=L1；TN Rrc_EncodeMsg_TN pa=- | `lv5/lv5_01_ctx_filled_at_top.c` |
| LV5-02 | 上下文由独立的 Init 函数填：长度多算了尾部，缓冲区按原长申请（写效应） | S5.2  A3·M5·P3+P2·V3·R3·X1·K0·G0 | 2027H1 | TP Rrc_EncodeSib_TP pa=记账；TN Rrc_EncodeSib_TN pa=记账 | `lv5/lv5_02_ctx_init_func.c` |
| LV5-03 | IE 长度沿另一条链（按类型查表、两层返回）得到，缓冲区沿主链下传 | S5.3  A3·M1·P1·V2·R3·X0·K1·G0 | 尽力 | TP Ue_OnIeInd_TP pa=L2；TN Ue_OnIeInd_TN pa=- | `lv5/lv5_03_len_from_sibling_chain.c` |
| LV5-04 | 中间层打开扩展字段时把长度加了，缓冲区没变 | S5.4  A3·M1·P3·V3·R2·X1·K0·G1 | 尽力（S5.4 里"本层先改字段再下传"的形态，用 S5.1 的帧内字段跟踪即可；被调者改写字段的形态见 LV5-02，2027H1） | TP Rrc_EncodeExt_TP pa=L1；TN Rrc_EncodeExt_TN pa=- | `lv5/lv5_04_field_changed_midway.c` |
| LV5-05 | 字段级申请（alloc_free_identity 里带 offset 的那类）：资源缓冲写尾越界 | S5.5  A1·M4·P3+P2·V0·R1·X0·K0·G0 | 尽力 | TP Ue_OnSetup_TP pa=L1；TN Ue_OnSetup_TN pa=- | `lv5/lv5_05_field_alloc_offset.c` |
| LV5-06 | 菱形：缓冲区从 A 链（取块，三层）、长度从 B 链（取配置，两层）分别回到链顶，再一起下传 | S4.1+S4.4 组合（R3）  A3·M4·P2+P1·V2·R3·X0·K0·G0 | 承诺（用 Lv4 的返回值效应就够） | TP Rpt_Flush_TP pa=L1；TN Rpt_Flush_TN pa=- | `lv5/lv5_06_diamond_buf_len.c` |
| LV6-01 | 跨文件：extern 不完整声明的全局统计表，下标常量从三层之上传入 | S6.1  A1·M2·P4·V1·R1·X0·K0·G0（跨 TU） | 尽力 | TP Port_OnLinkDown_TP pa=L1；TN Port_OnLinkDown_TN pa=- | `lv6/lv6_01_extern_tbl` |
| LV6-02 | 全局规格变量只在初始化函数里写常量（新规格 64），数组还是老规格 32 | S6.2  A1·M2·P0·V4·R1·X0·K2·G0 | 2027H1 | TP Cell_ResetUe_TP pa=记账；TN Cell_ResetUe_TN pa=记账 | `lv6/lv6_02_global_spec_len.c` |
| LV6-03 | 运行期申请的全局表：大小由 g_ulTblNum 决定，访问前按 > g_ulTblNum 校验（应为 >=） | S6.3  A1·M5·P4·V4·R1·X1·K1·G4 | 2027（程序分析验证 + AI 提不变式） | TP Svc_OnSetReq_TP pa=记账 ai+pa=L2；TN Svc_OnSetReq_TN pa=记账 ai+pa=- | `lv6/lv6_03_global_runtime_tbl.c` |
| LV6-04 | 端口数来自配置文件解析（运行期值），遍历全局端口表 | S6.4  A1·M2·P0·V4·R1·X0·K2·G0 | AI + 程序分析 | TP Board_InitPorts_TP pa=记账 ai+pa=L2；TN Board_InitPorts_TN pa=- | `lv6/lv6_04_global_cfg_from_file.c` |
| LV6-05 | 全局指针在初始化时指向静态内存池的尾部 64 字节，三层之下按 64 写结尾 | S6.3 变体  A1·M2·P4·V1·R1·X1·K0·G0 | 2027H1 | TP Om_SaveTrace_TP pa=记账；TN Om_SaveTrace_TN pa=记账 | `lv6/lv6_05_global_ptr_into_pool.c` |
| LV7-01 | 柔性数组：按 n 项申请，封口时写第 n 项 | S7.1  A1·M6·P2+P1·V1·R2·X1·K0·G0 | 2027H1（计划 P1） | TP List_Build_TP pa=记账；TN List_Build_TN pa=记账 | `lv7/lv7_01_flexible_array.c` |
| LV7-02 | 二维数组行级越界：列下标多走一个，越进了下一行（整数组范围内） | S7.2  A1·M7·P0·V1·R1·X1·K2·G0 | 计划 P1（CSA 盲区 #18，1–2 层也报） | TP Sched_OnSlot_TP pa=-；TN Sched_OnSlot_TN pa=- | `lv7/lv7_02_2d_row_overflow.c` |
| LV7-03 | 字节缓冲按消息结构体解释，三层之下写数据区——缓冲区本身比结构体短 | S7.3  A1·M1·P1·V1·R2·X4·K0·G0 | 2027H1 | TP Om_OnShortMsg_TP pa=记账；TN Om_OnShortMsg_TN pa=记账 | `lv7/lv7_03_struct_overlay.c` |
| LV7-04 | 循环中指针步进拷贝：个数来自三层之上，比目的缓冲区大 | S7.4  A2·M1·P1·V1·R2·X1·K3·G0 | 2027H1 | TP Om_DumpReg_TP pa=记账；TN Om_DumpReg_TN pa=记账 | `lv7/lv7_04_pointer_stepping.c` |
| LV7-05 | strcpy_s 的 destMax 用了名字的最大规格，目的缓冲区是老长度 | S7.5（destMax 那一半）  A4·M1·P1·V0·R1·X0·K0·G0 | 计划 P1（destMax 一半与 memcpy_s 相同；源串长度那一半 2027H2） | TP Cell_Rename_TP pa=-；TN Cell_Rename_TN pa=- | `lv7/lv7_05_strcpy_destmax.c` |
| LV7-06 | 二维表按"行数 × 列数"申请，访问用变量步长线性化（两个变量相乘） | S7.6  A1·M5·P2+P1·V1·R2·X5·K2·G0 | 不承诺 | TP Cell_BuildMap_TP pa=记账；TN Cell_BuildMap_TN pa=记账 | `lv7/lv7_06_variable_stride.c` |
| LV8-01 | TLV 解析循环：每轮的校验漏算了 2 字节 TLV 头（读越界） | S8.1（相对条件写法）  A3·M1·P1·V5·R2·X1·K3·G1 | 尽力 | TP Om_OnCfgMsg_TP pa=L2；TN Om_OnCfgMsg_TN pa=- | `lv8/lv8_01_tlv_relative_check.c` |
| LV8-02 | 两遍式 TLV：第一遍校验总长，第二遍按字节搬运时不再逐个校验 | S8.1（跨轮 / 跨循环关系）  A1·M1·P1·V5·R2·X1·K3·G4 | AI + 程序分析 | TP Om_OnBatchMsg_TP pa=记账 ai+pa=L2；TN Om_OnBatchMsg_TN pa=记账 ai+pa=- | `lv8/lv8_02_tlv_two_pass.c` |
| LV8-03 | 手写的字符串拷贝循环（while (*src)），链顶传了一个比目的缓冲区长的字面量 | S8.2+S7.5  A2·M1·P1·V1·R2·X1·K4·G0 | 2027H2（字符串长度抽象 + 指针步进） | TP Cell_SetDefault_TP pa=记账；TN Cell_SetDefault_TN pa=记账 | `lv8/lv8_03_handwritten_strcpy.c` |
| LV8-04 | 三维资源格（符号 × RB × 端口）按配置值线性化，嵌套循环多走了一个端口 | S8.3  A1·M5·P2+P1·V1·R2·X5·K4·G0 | 不承诺 | TP Phy_BuildGrid_TP pa=记账；TN Phy_BuildGrid_TN pa=记账 | `lv8/lv8_04_grid_3d_config.c` |
| LV8-05 | 环形缓冲区：尾指针回绕条件写成 > RING_LEN（应为 >=），访问在三层之下 | S8.1 变体（字段不变式）  A1·M2·P4·V4·R1·X0·K3·G4 | AI + 程序分析（也是 2027 字段级不变式的候选） | TP Om_Trace_TP pa=记账 ai+pa=L2；TN Om_Trace_TN pa=记账 ai+pa=- | `lv8/lv8_05_ring_tail_invariant.c` |
| LV9-01 | 同一函数里按制式选处理函数（函数指针取常量），NR 处理按"长度"位置写结尾 | S9.1  A1·M1·P5·V1·R2·X0·K1·G0 | 尽力（计划 P1） | TP Ue_OnCap_TP pa=L2；TN Ue_OnCap_TN pa=- | `lv9/lv9_01_local_fptr_select.c` |
| LV9-02 | 静态消息分发表（全局 const 结构体数组里的函数指针），其中一个处理函数写结尾越界 | S9.2  A1·M1·P5·V1·R2·X0·K1·G0 | 2027H1 | TP Msg_OnRecv_TP pa=记账；TN Msg_OnRecv_TN pa=记账 | `lv9/lv9_02_static_ops_table.c` |
| LV9-03 | 运行期注册的事件回调：注册在模块初始化里，触发在另一条链上 | S9.3  A1·M1·P5·V1·R2·X0·K1·G0 | AI + 程序分析（2027 的指向分析也能做一部分） | TP Evt_FireCellUp_TP pa=记账 ai+pa=L1；TN Evt_FireCellUp_TN pa=记账 ai+pa=- | `lv9/lv9_03_runtime_register.c` |
| LV9-04 | BSP 驱动接口只有声明：按 ulLen 往 pucBuf 读寄存器，调用方缓冲区 16、长度 32 | S9.4  A3·M1·P5·V1·R2·X0·K0·G0 | 模型表（程序分析） + AI 生成模型草稿 | TP Om_ReadChip_TP pa=记账 ai+pa=L1；TN Om_ReadChip_TN pa=记账 ai+pa=- | `lv9/lv9_04_external_driver.c` |
| LV9-05 | 递归遍历配置树，按深度写路径数组，深度保护差一 | S9.5  A1·M1·P1·V1·R2·X1·K5·G1 | 2027H1 | TP Cfg_DumpTree_TP pa=记账；TN Cfg_DumpTree_TN pa=记账 | `lv9/lv9_05_recursion_depth.c` |
| LV9-06 | 跨任务消息：发送方把"端口个数"填进了"端口号"字段，接收任务三层之后按端口号索引 | S9.6（v3.11 新增）  A1·M2·P5·V1·R1·X0·K0·G0 | AI + 程序分析 | TP Om_OnPortCfg_TP pa=记账 ai+pa=L1；TN Om_OnPortCfg_TN pa=记账 ai+pa=- | `lv9/lv9_06_intertask_msg.c` |
| LV10-01 | 报文结构体有两组"缓冲区 + 长度"，访问 ext 时用 dataLen 做了校验 | S10.1  A1·M8·P3·V3·R2·X0·K1·G4 | AI + 程序分析 | TP Drv_OnRxPkt_TP pa=记账 ai+pa=L2；TN Drv_OnRxPkt_TN pa=记账 ai+pa=- | `lv10/lv10_01_field_pairing.c` |
| LV10-02 | 平台 API 的"缓冲区 + 长度"约定只写在注释里，调用方传的长度大于缓冲区 | S10.2  A3·M1·P5·V0·R1·X0·K0·G4 | AI + 程序分析 | TP Om_QueryDev_TP pa=记账 ai+pa=L1；TN Om_QueryDev_TN pa=记账 ai+pa=- | `lv10/lv10_02_param_pairing_api.c` |
| LV10-03 | 对外接口注释承诺"端口号 0~31"，实现里的表只有 16 项且不校验 | S10.3  A1·M2·P0·V1·R1·X0·K0·G4 | AI + 程序分析 | TP Port_SetSpeed_TP pa=记账 ai+pa=L2；TN Port_SetSpeed_TN pa=记账 ai+pa=- | `lv10/lv10_03_doc_contract.c` |
| LV10-04 | 外部消息里的长度一路下传到 memcpy_s，没有任何校验（TN：框架在注册时声明了最大长度） | S10.4  A3·M2·P0·V5·R1·X0·K0·G0 | AI + 程序分析（候选研判） | TP Om_OnSaveCfg_TP pa=记账 ai+pa=L2；TN Om_OnSaveCfg_TN pa=记账 ai+pa=- | `lv10/lv10_04_unchecked_ext_len.c` |
| LV10-05 | "个数"字段与数组容量的对应关系：解码时直接从消息里取个数，没跟数组容量比 | S10.1 变体（个数 ↔ 数组）  A1·M3·P3·V3·R2·X1·K2·G4 | AI + 程序分析 | TP Cell_OnNbrMsg_TP pa=记账 ai+pa=L2；TN Cell_OnNbrMsg_TN pa=- | `lv10/lv10_05_count_field_writer.c` |
