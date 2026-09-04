# 正式文件规划

## 原则

文件按状态、时序和接口拆分

```text
State: 状态放哪里
Kernel: 什么时候允许变化
Trace: 变化怎么记录
BUS: 事务怎么传
CPU: 指令怎么跑
Device: 外设怎么响应
```

## 目录

```text
docs/
  01-simulation-kernel.md
  02-module-map.md
  03-formal-file-plan.md
  04-core-interface-contract.md
  05-ram-bus-compatible-contract.md
  06-cpu8086.md
  07-launcher.md
  08-pic8259-behavior.md
  09-pit8253-behavior.md
  10-cga-text-display.md
  11-ata-pio-disk.md
  12-keyboard8042-behavior.md
  13-pc-compatible-bios.md
  14-dma8237-behavior.md
```

## C 文件边界

- `sim_state.c`
- `sim_kernel.c`
- `sim_trace.c`
- `sim_bus.c`
- `sim_ram.c`
- `sim_rom.c`
- `cpu8086_state.c`
- `cpu8086.c`
- `cpu8086_decoder.c`
- `cpu8086_ea.c`
- `cpu8086_alu.c`
- `cpu8086_shifter.c`
- `cpu8086_eu.c`
- `cpu8086_biu.c`
- `cpu8086_prefetch.c`
- `sim_pic.c`
- `sim_pit.c`
- `sim_cga.c`
- `sim_keyboard.c`
- `sim_disk.c`
- `sim_dma.c`

