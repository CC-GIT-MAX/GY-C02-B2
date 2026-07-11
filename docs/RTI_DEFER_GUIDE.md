# RTI_Defer & RTI 时间管理使用指南

> 本指南面向后续业务模块开发者：什么时候用 RTI_Defer、什么时候用 tick 内状态机、什么时候才允许 busy-wait。

## 0. 三种"等一等"的方式

| 方式 | API | 阻塞？ | 适用 |
|---|---|---|---|
| 同步阻塞（禁止） | OSIF_TimeDelay / YTM_DELAY_xms | 是，整个 super-loop 卡住 | 硬件握手 ≤ 100us |
| 同步短延（唯一合法阻塞） | YTM_DELAY_1us（N 个 NOP） | 是，CPU 死等几 us | 等 ADC / I2C / SPI 标志 |
| 异步延后 | RTI_Defer(ms, cb, ctx) | 否，立刻返回 | N ms 后跑 cb |
| 周期性 tick | RTI_OpenSlot + RTI_SlotElapsed | 否 | 5/10/50/100ms 子任务 |
| 状态机延时 | RTI_GetTick1ms + 差值比较 | 否 | tick 内多步时序 |

**架构硬约束**（docs/ARCHITECTURE.md § 3）：

- 永不自己写 or (volatile u32 i = 0; i < N; i++); 空转延时
- 永不直接读 LPTMR / SysTick 寄存器
- 永不自己持 static uint32_t last_tick = 0; 私有定时器
- 想要"等 N ms 后做 X"——必须用本指南列出的合法方式
## 1. YTM_DELAY_1us — 唯一允许的阻塞

**何时用**：硬件握手 / ≤ 100us / 必须 timeout 退出 / 不在 ISR 内。

**正例**：

    u32 guard = 100u;
    while (!(ADC0->STATUS & ADC_STATUS_COCO_MASK)) {
        YTM_DELAY_1us();
        if (--guard == 0u) return C02B2_ERR_TIMEOUT;
    }

**反例**：

    for (u32 i = 0; i < 12000; i++) { YTM_DELAY_1us(); }   /* 12ms 阻塞 */

**为什么不写 YTM_DELAY_xms**：NOP 数量与时钟、编译器优化强相关，跨工具链不可移植。
## 2. RTI_Defer — 异步延后执行

**API**：

    typedef void (*rti_defer_cb_t)(void *ctx);
    c02b2_result_t RTI_Defer(uint32_t delay_ms, rti_defer_cb_t cb, void *ctx);
    c02b2_result_t RTI_DeferCancel(rti_defer_cb_t cb, void *ctx);
    void           RTI_DeferTick(void);   /* 由 Scheduler_Run 自动调 */
    void           RTI_DeferInit(void);   /* 由 RTI_Init 自动调 */
    uint32_t       RTI_DeferPending(void);

**约束**：8 个 slot / 不在 ISR 调 / callback 短非阻塞 / ctx 用 static state。

### 场景 1：一次性延后

    static void on_after_tx(void *ctx) {
        struct my_state *s = ctx;
        s->ack_deadline_reached = true;
    }
    CanIf_Send(CAN_CH_PUBLIC, &s->req_frame);
    RTI_Defer(50, on_after_tx, s);  /* 50ms 后 on_after_tx(s) 在 tick 里跑 */

### 场景 2：周期心跳（自重启）

    static void on_heartbeat(void *ctx) {
        WDG_Feed();
        CanIf_Send(CAN_CH_PUBLIC, &((struct hb_ctx*)ctx)->frame);
        RTI_Defer(100, on_heartbeat, ctx);   /* 永远只占 1 slot */
    }

### 场景 3：debounce

    void on_key_event(void) {
        /* 50ms 内重复调就 REPLACE deadline */
        RTI_Defer(50, on_key_debounced, NULL);
    }

### 场景 4：多路独立超时

    RTI_Defer(30, on_timeout_a, ctx);  /* slot 0 */
    RTI_Defer(50, on_timeout_b, ctx);  /* slot 1 */
    RTI_Defer(80, on_timeout_c, ctx);  /* slot 2 */
## 3. 多步时序控制序列 — 两种方案

### 原型（busy-wait 阻塞 — 禁止）

    if (Check_Bl_EN_Fault_Flag != 0) {
        PORT_LCD_EN_O;  PORT_LCD_EN_L;       /* step 1: 拉低 */
        PORT_LCD_PWM_O; PORT_LCD_PWM_L;
        YTM_DELAY_xms(10);                   /* 阻塞 10ms - 禁止 */
        PORT_LCD_EN_O;  PORT_LCD_EN_H;       /* step 2: 拉高 */
        YTM_DELAY_xms(10);                   /* 阻塞 10ms - 禁止 */
        PCTRLD->PCR[2] = 0x0200;             /* step 3: 写寄存器 */
        Check_Bl_EN_Fault_Flag = 0;
    }

**为什么不能直译**：阻塞 20ms = 整个 super-loop 卡死 = 其它模块停转 = watchdog 可能复位。
### 方案 A（推荐）：tick 内 3-phase 状态机

**适用**：控制序列全在你的模块里（GPIO 翻转 + 寄存器写），不依赖外部硬件等待。

    typedef enum {
        LCD_BL_IDLE,
        LCD_BL_PHASE_LOW,
        LCD_BL_PHASE_HIGH,
    } lcd_bl_phase_t;

    static struct {
        lcd_bl_phase_t phase;
        uint32_t       phase_start_ms;
    } s_lcd_bl = { LCD_BL_IDLE, 0u };

    static void prv_tick(void) {
        if (Check_Bl_EN_Fault_Flag != 0u && s_lcd_bl.phase == LCD_BL_IDLE) {
            Check_Bl_EN_Fault_Flag = 0u;
            BL_EN_RESET_IS_PRINTF_FLAG = 1u;
            PORT_LCD_EN_O;  PORT_LCD_EN_L;
            PORT_LCD_PWM_O; PORT_LCD_PWM_L;
            s_lcd_bl.phase = LCD_BL_PHASE_LOW;
            s_lcd_bl.phase_start_ms = RTI_GetTick1ms();
        }
        switch (s_lcd_bl.phase) {
        case LCD_BL_PHASE_LOW:
            if ((u32)(RTI_GetTick1ms() - s_lcd_bl.phase_start_ms) >= 10u) {
                PORT_LCD_EN_O;  PORT_LCD_EN_H;
                s_lcd_bl.phase = LCD_BL_PHASE_HIGH;
                s_lcd_bl.phase_start_ms = RTI_GetTick1ms();
            }
            break;
        case LCD_BL_PHASE_HIGH:
            if ((u32)(RTI_GetTick1ms() - s_lcd_bl.phase_start_ms) >= 10u) {
                PCTRLD->PCR[2] = 0x0200u;
                s_lcd_bl.phase = LCD_BL_IDLE;
            }
            break;
        case LCD_BL_IDLE:
        default:
            break;
        }
    }

**优点**：不消耗 RTI_Defer slot / 总时序 20ms / 中间可加日志 + timeout。
**限制**：不能等外部硬件 > 1ms。
### 方案 B：RTI_Defer 链

**适用**：控制序列穿插其他模块、跨模块协调。

    typedef enum {
        BL_RECOVER_IDLE,
        BL_RECOVER_STEP1,
        BL_RECOVER_STEP2,
        BL_RECOVER_STEP3,
    } bl_recover_state_t;

    static struct { bl_recover_state_t state; } s_bl_recover = { BL_RECOVER_IDLE };

    static void prv_step1(void *ctx) {
        (void)ctx;
        PORT_LCD_EN_O;  PORT_LCD_EN_L;
        PORT_LCD_PWM_O; PORT_LCD_PWM_L;
        (void)RTI_Defer(10, prv_step2, &s_bl_recover);
    }

    static void prv_step2(void *ctx) {
        (void)ctx;
        PORT_LCD_EN_O;  PORT_LCD_EN_H;
        (void)RTI_Defer(10, prv_step3, &s_bl_recover);
    }

    static void prv_step3(void *ctx) {
        (void)ctx;
        PCTRLD->PCR[2] = 0x0200u;
        Check_Bl_EN_Fault_Flag = 0u;
        s_bl_recover.state = BL_RECOVER_IDLE;
    }

    c02b2_result_t Bl_RecoverStart(void) {
        if (s_bl_recover.state != BL_RECOVER_IDLE) return C02B2_ERR_BUSY;
        s_bl_recover.state = BL_RECOVER_STEP1;
        return RTI_Defer(0, prv_step1, &s_bl_recover);
    }

    /* tick 里检测 + 启动 */
    static void prv_tick(void) {
        if (Check_Bl_EN_Fault_Flag != 0u) {
            Check_Bl_EN_Fault_Flag = 0u;
            BL_EN_RESET_IS_PRINTF_FLAG = 1u;
            (void)Bl_RecoverStart();
        }
    }

**优点**：10ms 间隔里 super-loop 跑其他模块 / 跨模块协调容易 / 永远只占 1 slot。
**限制**：step 之间有 ≤ 1ms 额外延迟 / 必须每个 callback arm 下一步。
## 4. 方案选择决策表

| 场景 | 推荐 |
|---|---|
| 等 ADC EOC（≤ 100us 硬件） | YTM_DELAY_1us 循环 + timeout |
| 单次延后做某事 | RTI_Defer(ms, cb, ctx) |
| 周期性 tick | RTI_OpenSlot + RTI_SlotElapsed |
| 多步控制序列（寄存器+GPIO） | 方案 A：tick 内状态机 |
| 多步控制序列（跨模块协调） | 方案 B：RTI_Defer 链 |
| 等待外部硬件 > 1ms | 拆状态机 + YTM_DELAY_1us 短延 + 标志位 |
| 心跳 / 看门狗喂狗 | RTI_Defer 自重启 |
| 防抖 | RTI_Defer 同 (cb, ctx) 重调 |

## 5. 常见错误

### 错 1：ms 级忙等

    for (u32 i = 0; i < 12000; i++) { YTM_DELAY_1us(); }   /* 12ms 阻塞 */

→ 改用 RTI_Defer 或状态机。

### 错 2：ISR 内调 RTI_Defer

    void SysTick_Handler(void) {
        RTI_OnTick1ms();
        RTI_Defer(10, cb, ctx);   /* 错 */
    }

→ ISR 只更新硬件标志，tick 里检查 + arm。

### 错 3：callback 阻塞

    static void bad_cb(void *ctx) {
        while (!flag);   /* 错 */
    }

→ callback 必须短：设标志 / 发 CAN 帧 / 改状态。

### 错 4：ctx 传栈变量

    void func(void) {
        struct s local = {0};
        RTI_Defer(50, cb, &local);   /* 错 */
    }

→ 用模块 static struct s g_ctx;。

### 错 5：序列中途不 arm 下一步

    static void step1(void *ctx) {
        do_step1();
        /* 漏了 RTI_Defer(10, step2, ctx) */
    }

→ step1 跑完序列停在那，step2/step3 永远不跑。

### 错 6：两个 RTI_Defer 模拟"间隔"

    /* 想 "A 完后 10ms 做 B" — 错 */
    RTI_Defer(10, do_a, NULL);   /* 10ms 后 A */
    RTI_Defer(20, do_b, NULL);   /* 20ms 后 B（不是 A 完后 10ms） */

A 和 B 都按绝对时间触发。如果要"A 完后 10ms 做 B"：

- 用 RTI_Defer 链（方案 B）
- 或用 tick 内状态机（方案 A）
## 6. 速查

    想"等 X 再继续"？
    |
    |-- X <= 100us 且是硬件握手
    |     -- YTM_DELAY_1us() 循环 + timeout   <- 唯一允许的阻塞
    |
    |-- X = 5/10/50/100ms 且周期性
    |     -- RTI_OpenSlot + RTI_SlotElapsed  <- 周期 tick
    |
    |-- X 任意 ms 且延后做单件事
    |     -- RTI_Defer(X, cb, ctx)            <- 异步延后 (8-slot)
    |
    -- 多步时序 (A -> 10ms -> B -> 10ms -> C)
          |-- 步骤全在同模块、只动 GPIO/寄存器
          |     -- 方案 A: tick 内 3-phase 状态机 (推荐)
          -- 跨模块协作 / 等外部硬件就绪
                -- 方案 B: RTI_Defer 链 (按阶段注册回调)

## 7. 参见

- pp/rti/rti.h - RTI API 完整定义 (RTI_GetTick1ms / RTI_OpenSlot / RTI_SlotElapsed)
- pp/rti/rti_defer.h - RTI_Defer API 完整定义
- pp/can/can_rx.c - 周期 tick 使用范例 (50ms 超时监控)
- pp/mod_template/mod_template.c - 10ms / 100ms 子任务范例
- docs/ARCHITECTURE.md 3 - 模块四件套 / 禁止事项 (含本规约)