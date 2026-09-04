# 核心接口契约

## 状态

`SimState` 保存整机公共状态。`Cpu8086State` 保存 CPU 专属状态。两者都遵守 current / next 的双缓冲规则。

## 挂载方式

每个模块通过 `SimModule` 接入内核。

模块提供四类回调：

- `reset`：复位当前状态和下一状态；
- `drive`：在组合阶段提出事务和候选输出；
- `sample`：根据总线裁决结果写入 next；
- `trace`：记录当前拍到下一拍的变化。

## 数据流

```text
current state
    -> module drive
    -> drive requests
    -> bus resolve
    -> module sample
    -> next state
    -> rising edge commit
```

## 总线接口

`SimBusRequest` 描述一次请求，`SimBusResponse` 描述一次响应，`SimBusState` 保存当前事务的阶段。

总线事务区分三类：

- memory
- I/O
- interrupt acknowledge

## 兼容要求

- 模块不能直接改别的模块的 current 状态；
- 读 current，写 next；


## 设计意图

这套接口保留了寄存器级仿真的思路，也给后面的 CPU、PIC、PIT、CGA 留了统一接法。

