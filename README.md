# C02-B2 仪表 MCU 项目

## 简介

基于 YTM32B1MD1 的汽车仪表 MCU 软件。裸机 + RTI 时间片调度，IAR 9.x 编译。

## 目录结构

```
app/        业务模块入口（main.c / scheduler / signal / log / 各业务模块）
board/      板级配置（时钟 / 引脚 / 各外设 config）
middleware/ 通用中间件（printf / osif）
platform/   厂商 SDK（devices / drivers）
rtos/       OSIF 适配
EWARM/      IAR 工程文件
docs/       架构与设计文档
```

## 构建

使用 IAR Embedded Workbench 打开 `EWARM/C02_B2.eww`，选择 FLASH 配置，Rebuild All。

## 优化项实施记录

参见 `docs/OPTIMIZATION_PLAN.md` 与各 commit 历史。

## 优化项
