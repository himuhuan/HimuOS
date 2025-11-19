## BRANCH [refactor/hostdlib-int-to-string]

### [2025-11-19] refactor(hostdlib): update integer-to-string utilities and cleanup

BREAKING CHANGE:
1. 现在 `Int64ToString[Ex]` 只保留基于 10 的转换，移除对其他进制的支持。
2. 移除了 `FormatString` 函数，该函数将在未来版本中重新设计和实现。
3. `32` 位整数转换函数已被移除。

CHANGES:
1. 添加 `*Ex` 系列函数以支持更灵活的 padding 选项。
2. 修改了其它模块中对这些函数的调用以适应新的接口。

