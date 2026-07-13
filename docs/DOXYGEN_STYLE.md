# Doxygen 注释规范

> 本规范自 `130fde5d`（import baseline）之后所有**新增**函数强制使用。
> 旧代码允许保留原样，但改写时必须按本规范补齐。
> 由 `tools/check_doxygen.sh` 在 CI 与本地 pre-commit 中强制。

## 1. 适用范围

| 函数类型 | 是否需要 | 说明 |
|----------|----------|------|
| 头文件 `.h` 中声明的公开 API | ✅ **必须**完整注释 | 别人会调用 |
| `.c` 中非 `static` 函数 | ✅ **必须**完整注释 | 跨文件可见 |
| `.c` 中 `static` 函数 | ⚠️ **必须** `@brief`（1 行即可） | 内部 helper |
| 厂商 SDK / 第三方代码 | ❌ 不加 | 不在维护范围 |

## 2. 标准模板（**严格遵守**）

> **中英文 `@brief` 必须各写一行，不可省略任何一行。**
> **每个参数都需 `@param[in] / @param[out] / @param[in,out]`。**
> **非 void 返回值必须有 `@return`；多返回值场景用 `@retval` 列表。**

### 2.1 完整模板

```c
/**
 * @brief   Open the JTAG/SWD debug interface by clearing the DBG disable bits
 *          in the CUS NVR configuration area. A system software reset is
 *          triggered after the NVR is successfully reprogrammed.
 * @brief   通过清除 CUS NVR 配置区中的 DBG 禁用位来开启 JTAG/SWD 调试接口.
 *          NVR 成功烧录后会自动触发软件复位.
 *
 * @param[in]      arg1     说明（含单位/范围/默认值）
 * @param[out]     arg2     说明
 * @param[in,out]  arg3     说明
 *
 * @return  status_t  STATUS_SUCCESS on success, otherwise STATUS_ERROR.
 * @retval  0        成功
 * @retval  -1       参数无效
 *
 * @note    （可选）使用注意事项、副作用、并发要求
 * @warning （可选）危险调用、误用后果
 * @see     （可选）相关函数引用
 */
```

### 2.2 强制约束

- `/**` 开头，`*/` 结尾（**不能用** `/* ... */` 单星号）
- 第一个 `@brief` 用**英文**，第二个 `@brief` 用**中文**
- 中文 `@brief` 用全角标点（`，。：；`），句末加句号
- 英文 `@brief` 用半角标点（`, . : ;`），句末加句号
- `@param` **三个标签缺一不可**：`[in]` / `[out]` / `[in,out]`
- `@param` 顺序与函数形参顺序一致
- `@return` 与函数返回类型对齐；void 函数**必须省略** `@return`
- 多返回值时**两者并用**：`@return` 描述总类型 + `@retval` 列具体值
- `@note` / `@warning` / `@see` 可选

### 2.3 行宽

- 每行 < 100 字符（保持 diff 可读）
- 中英文 `@brief` 可分多行（每行续行以空格开头，缩进 4 空格对齐）

## 3. 实例

### 3.1 公开 API（有入参、有返回值）

```c
/**
 * @brief   Read KL30 voltage from ADC channel and convert to mV
 * @brief   从 ADC 通道读取 KL30 电压并转换为 mV
 *
 * @param[in]  channel  ADC channel index (0..BOARD_ADC_CH_MAX-1)
 *
 * @return  u16  Voltage in mV (0 if ADC not ready)
 */
u16 Board_ADC_ReadRaw_mV(u8 channel);
```

### 3.2 无返回值（void）

```c
/**
 * @brief   Initialize the power management module
 * @brief   初始化电源管理模块
 *
 * @param[in]  cold_boot  1 = cold boot (KAM lost), 0 = warm boot (KAM preserved)
 *
 * @note    Must be called once before Scheduler_Init().
 */
void Power_Init(u8 cold_boot);
```

### 3.3 内部 static 函数（**最少** 1 行 `@brief`）

```c
/** @brief  Compute low-pass filtered battery voltage (1st-order IIR, alpha=1/8) */
static u16 prv_filter_bat_mv(u16 raw_mv);
```

## 4. 头文件 vs .c 文件

| 位置 | 注释放在哪 |
|------|-----------|
| 函数声明（`X.h`） | **必须**有完整 Doxygen 注释 |
| 函数实现（`X.c`） | **必须**复述完整 Doxygen 注释（CI 会扫描 .c 中的非 static 函数） |

> Doxygen 默认会从 `.h` 拉注释，但我们 CI 扫描的是 `.c` 文件，所以**两边都要写**。
> 实现特有信息（如 `@note` 副作用）放 `.c`。

## 5. CI / 自动化检查

`tools/check_doxygen.sh` 扫描：
- 头文件中所有函数声明必须带 `/** @brief`
- `.c` 文件中的非 `static` 函数必须带 `/** @brief`
- 每个文件至少出现 1 次英文 `@brief` + 1 次中文 `@brief`（双 @brief 检查）
- 内部 `static` 函数**至少** 1 行 `/** @brief ... */`

CI 失败示例：
```
[FAIL] app/foo.c:42: function 'bar' missing /** @brief above
[FAIL] app/foo.h:18: function 'baz' has 2 params but only 1 @param documented
[FAIL] app/foo.c:55: function 'qux' has only English @brief, missing Chinese @brief
```

## 6. 与 CI 集成

在 GitHub Actions 的 `lint-job` 中：
```yaml
- name: Doxygen check
  run: bash tools/check_doxygen.sh
```

## 7. DOXYGEN_STYLE.md 自查清单

提交前自查：

- [ ] 每个 `.h` 中的函数都有完整注释
- [ ] 每个 `.c` 中的非 static 函数都有完整注释
- [ ] 每个函数都**有 2 行 @brief**（英文 + 中文）
- [ ] 每个参数都标注了 `[in] / [out] / [in,out]`
- [ ] 非 void 函数都有 `@return`
- [ ] `bash tools/check_doxygen.sh` 输出 `PASSED`
