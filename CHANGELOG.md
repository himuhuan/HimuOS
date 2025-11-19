## BRANCH [refactor/hostdlib-int-to-string] (merged)

### [2025-11-19] refactor(hostdlib): update integer-to-string utilities and cleanup

BREAKING CHANGE:
1. 现在 `Int64ToString[Ex]` 只保留基于 10 的转换，移除对其他进制的支持。
2. 移除了 `FormatString` 函数，该函数将在未来版本中重新设计和实现。
3. `32` 位整数转换函数已被移除。

CHANGES:
1. 添加 `*Ex` 系列函数以支持更灵活的 padding 选项。
2. 修改了其它模块中对这些函数的调用以适应新的接口。

## BRANCH [feature/console-kprintf-padding]

### [2025-11-19] feat(console): implement format padding and stricter parsing

BREAKING CHANGE:
1. **严格的格式检查**：遇到未知的格式说明符（default case）时，内核现在会触发 `HO_KPANIC` (EC_ILLEGAL_ARGUMENT)，而不是原样输出字符。
2. `%l` 说明符行为变更：必须显式跟随子类型（如 `%ld`, `%lu`, `%lx`），不再支持单独使用 `%l`。
3. 移除对怪异格式的支持：如 `%ul` 等不再被支持。

CHANGES:
1. **实现格式化填充**：`ConsoleWriteFmt` 现在支持解析宽度和填充字符（支持空格和 `0` 填充）。
   - 支持字符串 `%s` 的宽度对齐。
   - 支持整数类型通过调用新版 `*Ex` 函数实现填充。
2. **扩展 Long 类型支持**：完善了 `long` 类型的处理逻辑，显式支持 `%ld`, `%li`, `%lu`, `%lx/%lX`。
3. **重构**：
   - 优先处理 `%%` 转义，优化解析流程。
   - 将整数打印逻辑迁移至 `hostdlib` 的 `Int64ToStringEx` 和 `UInt64ToStringEx` 接口。
4. **更新调用代码**：修改了 `kprintf` 调用以适应新的格式化逻辑。