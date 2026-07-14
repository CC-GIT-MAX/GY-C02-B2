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

> **🔴 强约束：.h 文件中**禁止**使用 @details（公开 API 的契约应通过 @param / @return / @retval 表达，不在头文件中暴露实现细节）。
> **🟡 软约束：.c 文件中**按需**使用 @details——仅在函数实现较复杂（多步骤 / 有副作用 / 有状态依赖 / 实现 > 5 行）时推荐补充；简单函数（单一 SDK 调用、无分支、< 5 行）省略。


> **必须有中文 `@brief`（一句），可附 1 行英文 `@brief`（一句）。**
> **多个参数按函数形参顺序逐行写 `@param[in]` / `@param[out]` / `@param[in,out]`；无参不写。**
> **非 void 返回必须写 `@return`；多返回值用 `@retval` 列表。**
> **`@details`（中文）：`.c` 文件推荐补充实现细节；`.h` 文件一般不写。**

### 2.1 `.c` 文件模板

```c
/**
 * @brief   Fetch the most recent raw frame for an IPK CAN id
 * @brief   获取某 IPK CAN id 最近一次的原始帧
 *
 * @details RX tick 在每个 IPK can_id 上缓存最近收到的原始 8 字节
 *          payload，diag / demo 模块可读整帧而无需重新解码每个信号。
 *          返回自启动以来收到的最后一帧（冷复位时清空）。 *
 * @param[in]   can_id  Standard 11-bit IPK can_id
 * @param[out]  out     Populated with the cached frame on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Frame returned (may be stale)
 * @retval  C02B2_ERR_PARAM     out is NULL or can_id not in IPK table
 * @retval  C02B2_ERR_NOT_FOUND No frame received yet for this can_id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out)
```

### 2.2 `.h` 文件模板

```c
/**
 * @brief   Copy the most recent raw 8-byte payload of a CAN frame
 * @brief   复制指定 CAN id 最近收到的 8 字节原始 payload

 * @param[in]   can_id  IPK RX can_id (11-bit standard)
 * @param[out]  out     Filled on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Cache populated
 * @retval  C02B2_ERR_PARAM     can_id not an IPK RX message, or out NULL
 * @retval  C02B2_ERR_NOT_FOUND No frame has been received for this id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out);
```

### 2.3 强制约束

- `/**` 开头，`*/` 结尾（**不能用** `/* ... */` 单星号）
- **第一个 `@brief` 用英文**（短句，可选但推荐），**第二个 `@brief` 用中文**（必填，祈使句）
- 中文 `@brief` 用全角标点（`，。：；`），句末加句号
- 英文 `@brief` 用半角标点（`, . : ;`），句末加句号
- `@param` 标签**只写适用的方向**：`[in]` / `[out]` / `[in,out]`；无参时**整个 `@param` 段都不写**
- `@param` 顺序与函数形参顺序一致
- `@return` 与函数返回类型对齐；void 函数**必须省略** `@return`
- 多返回值时**两者并用**：`@return` 描述总类型 + `@retval` 列具体值
- `@details`（中文）：`.c` 函数**按需**补充（见 §2 顶部强约束）；`.h` 函数**禁止**（不写）
- `@note` / `@warning` / `@see` 可选
- 注释主体仍以**中文为主**（说明、@details、@note 等尽量用中文）

### 2.4 行宽

- 每行 < 100 字符（保持 diff 可读）
- 中英文 `@brief` / `@details` 可分多行（每行续行以空格开头，缩进 4 空格对齐）

## 3. 实例

> 与 §2 模板完全相同，作为日常复制起点。`.c` 与 `.h` 复述同套注释（CI 扫描 `.c` 文件）。

### 3.1 公开 API 实现（`.c`）

```c
/**
 * @brief   Fetch the most recent raw frame for an IPK CAN id
 * @brief   获取某 IPK CAN id 最近一次的原始帧
 *
 * @details RX tick 在每个 IPK can_id 上缓存最近收到的原始 8 字节
 *          payload，diag / demo 模块可读整帧而无需重新解码每个信号。
 *          返回自启动以来收到的最后一帧（冷复位时清空）。
 *
 * @param[in]   can_id  Standard 11-bit IPK can_id
 * @param[out]  out     Populated with the cached frame on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Frame returned (may be stale)
 * @retval  C02B2_ERR_PARAM     out is NULL or can_id not in IPK table
 * @retval  C02B2_ERR_NOT_FOUND No frame received yet for this can_id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out)
```

### 3.2 公开 API 声明（`.h`）

```c
/**
 * @brief   Copy the most recent raw 8-byte payload of a CAN frame
 * @brief   复制指定 CAN id 最近收到的 8 字节原始 payload
 *
 * @param[in]   can_id  IPK RX can_id (11-bit standard)
 * @param[out]  out     Filled on success
 *
 * @return  c02b2_result_t
 * @retval  C02B2_OK            Cache populated
 * @retval  C02B2_ERR_PARAM     can_id not an IPK RX message, or out NULL
 * @retval  C02B2_ERR_NOT_FOUND No frame has been received for this id
 */
c02b2_result_t CanRx_GetLastRawFrame(u32 can_id, can_msg_t *out);
```

### 3.3 无返回值（void）

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

### 3.4 无参数函数

```c
/**
 * @brief   Reset cached raw frames
 * @brief   清空已缓存的原始帧
 */
void CanRx_Reset(void);
```

### 3.5 内部 static 函数（**最少** 1 行 `@brief`）

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
- 头文件中所有函数声明必须带 `/**` 中文 `@brief`
- `.c` 文件中的非 `static` 函数必须带 `/**` 中文 `@brief`
- 每个函数必须有中文 `@brief`；英文 `@brief` 可选
- `.c` 文件函数**推荐**含中文 `@details`（非强制）
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
- [ ] 每个函数都**有中文 `@brief`**（推荐再附 1 行英文 `@brief`）
- [ ] `.c` 文件函数**按需**含中文 `@details`（简单函数可省），`.h` 函数**禁止** `@details`
- [ ] `@param` 只在有参数时写，并标注 `[in] / [out] / [in,out]`
- [ ] 非 void 函数都有 `@return`（多返回值加 `@retval`）
- [ ] `bash tools/check_doxygen.sh` 输出 `PASSED`
