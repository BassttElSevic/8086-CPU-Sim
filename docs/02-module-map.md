# 模块依赖图

## 读法

实线表示运行时依赖。虚线表示验证关系。

## 当前总图

```mermaid
flowchart TD
    TYPES[sim_types]
    STATE[sim_state]
    TRACE[sim_trace]
    KERNEL[sim_kernel]
    BUS[sim_bus]
    RAM[sim_ram]
    ROM[sim_rom]
    CPU[cpu8086]
    PIC[sim_pic]
    PIT[sim_pit]
    CGA[sim_cga]
    KBD[sim_keyboard]
    DISK[sim_disk]
    DMA[sim_dma]
    BIOS[firmware BIOS]
    LAUNCHER[apps 启动器]

    TYPES --> STATE
    TYPES --> TRACE
    TYPES --> BUS
    TYPES --> CPU
    TYPES --> PIC
    TYPES --> PIT
    TYPES --> CGA
    TYPES --> KBD
    TYPES --> DISK
    TYPES --> DMA

    STATE --> KERNEL
    TRACE --> KERNEL
    STATE --> BUS
    BUS --> RAM
    BUS --> ROM
    BUS --> PIC
    BUS --> PIT
    BUS --> CGA
    BUS --> KBD
    BUS --> DISK
    BUS --> DMA

    CPU --> KERNEL
    PIC --> KERNEL
    PIT --> KERNEL
    CGA --> KERNEL
    KBD --> KERNEL
    DISK --> KERNEL
    DMA --> KERNEL

    ROM --> BIOS
    BIOS --> CPU
    LAUNCHER --> KERNEL
    LAUNCHER --> CGA
```

## 分层

- 基础层：`sim_types`、`sim_state`、`sim_trace`
- 调度层：`sim_kernel`、`sim_bus`
- 存储层：`sim_ram`、`sim_rom`
- 处理器层：`cpu8086`
- 外设层：`sim_pic`、`sim_pit`、`sim_cga`、`sim_keyboard`、`sim_disk`、`sim_dma`
- 软件层：BIOS、DOS
- 宿主层：启动器


