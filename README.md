# CH584_Template（AI 生成的类 HAL 库工程）

> ## ⚠️ 重要声明
>
> 1. **本工程对外设驱动（`Drivers/CH58x_HAL_Driver`）与板级支持包（`BSP`）是 AI 生成的"类 HAL 库"代码**，接口风格仿照 STM32 HAL，用于快速搭建工程骨架。
> 2. **寄存器定义取自 WCH 数据手册**：SFR 地址、位域与寄存器取值均按 CH584/CH585 手册填写，未使用猜测值。
> 3. **全部代码均未经过实机验证**：时钟/GPIO 初始化、各外设时序、中断处理、BLE/RF/协议栈对接、链接脚本等均未在真实硬件上运行确认，仍可能存在驱动逻辑错误、时序偏差、资源冲突甚至无法启动的问题。
> 4. 建议仅将此工程作为**结构与接口参考**。正式使用前，请对照 WCH 官方 SDK 验证功能，并自行承担由此产生的风险。

---

## 1. 工程概况

| 项目 | 内容 |
| --- | --- |
| 目标芯片 | CH584M（CH584/CH585 系列，RISC-V） |
| 芯片关系 | CH584 是 CH585 的**裁剪型号**，两者**共用同一 SVD 与同一套官方库**，工程中的 `CH585.svd`、`startup_CH585.S`、`ISP585.h`、`-lISP585` 均属正常用法 |
| 内核/架构 | `rv32imac`，ABI `ilp32`，code-model `medlow` |
| 构建系统 | VS Code + EIDE 插件（`type: RISC-V`，`toolchain: RISCV_GCC`） |
| 输出 | `build/Debug/${workspaceFolderBasename}.elf` |
| 链接脚本 | `Drivers/Device/CH584_FLASH.ld` |
| 依赖库 | `StdPeriphDriver`、`Drivers/CH58x_HAL_Driver/LIB`、`.../LIB/BLE` |
| 寄存器定义 | 依据 WCH CH584/CH585 数据手册填写（SFR 地址与位域） |

目录结构（主要部分）：

```
CH584_Template/
├── Core/                    # main.c、中断入口、HAL 配置头
├── BSP/                     # adc / i2c / lcd / pwm / spi / tim / uart / debug
├── Drivers/
│   ├── CH58x_HAL_Driver/    # 类 HAL 驱动（inc / src / LIB）
│   └── Device/              # SFR 头、core_riscv、启动文件、链接脚本、SVD
├── Protocol/                # ble / rf / stack 协议层
├── .eide/eide.yml           # EIDE 工程与构建/烧录配置
└── .vscode/launch.json      # 运行和调试（cortex-debug + OpenOCD）
```

---

## 2. 环境要求

### 2.1 打开工程（开箱即用）

工程根目录自带工作区文件 **`CH584_Template.code-workspace`**，直接双击打开（或 VS Code 菜单「文件 → 打开工作区…」）即可：

- EIDE 插件会自动识别同目录下的 `.eide/eide.yml`，**不需要再手动导入工程**；
- 工作区 `settings` 中已写入工具链设置（`EIDE.RISCV.InstallDirectory`、`EIDE.RISCV.ToolPrefix`、`EIDE.OpenOCD.ExePath`，见 3.1），按 `../../` 相对布局指向 WCH 工具目录；
- 打开后按提示安装推荐扩展（`cl.eide` 等）即可编译、下载。

也可以直接「打开文件夹」`CH584_Template`，EIDE 同样能识别 `.eide` 工程。

### 2.2 依赖清单

| 依赖 | 说明 |
| --- | --- |
| VS Code + EIDE 插件 | 构建、下载均通过 EIDE 完成 |
| WCH **RISC-V Embedded GCC 15** | 编译器前缀 `riscv32-wch-elf-`（`riscv32-wch-elf-gcc`） |
| WCH **OpenOCD** | `wch_openocd`，含 `openocd.exe` 与 `wch-riscv.cfg` |
| WCH-Link 及驱动 | 下载与在线调试 |
| `bash` | 烧录命令通过 Shell 脚本执行；Windows 下需 Git Bash / MSYS2 等提供 `bash` |
| cortex-debug 扩展 | `launch.json` 中的调试类型为 `cortex-debug` |

工程默认假设如下目录布局（即 `../../` 指向 WCH 工具根目录）：

```
<任意目录>/
├── Toolchain/RISC-V Embedded GCC15/       # WCH 提供的 GCC15
├── wch_flash/
│   ├── wch_openocd.sh                     # 烧录脚本
│   └── wch_openocd/bin/
│       ├── openocd.exe
│       └── wch-riscv.cfg
└── CH58X/CH584_Template/                  # 本工程
```

> 环境不同时，下面的三处配置**必须重新指定**，否则会出现"找不到编译器 / 无法启动调试 / 无法烧录"。

---

## 3. 在其他环境中使用前，必须修改的配置

### 3.1 EIDE 工具链设置（工作区）—— 指定 GCC15 与 OpenOCD

**位置**：工作区文件 `CH584_Template.code-workspace` → `settings`（等价于 EIDE 插件侧边栏 → **工具链设置（工作区）**）。

需要重新指定的是以下三项（当前值基于 `../../` 相对布局，迁移后按实际路径修改）：

| 设置项 | 当前值 | 含义 |
| --- | --- | --- |
| `EIDE.RISCV.InstallDirectory` | `..\..\Toolchain\RISC-V Embedded GCC15` | WCH GCC15 安装目录（`bin` 下应有 `riscv32-wch-elf-gcc.exe`） |
| `EIDE.RISCV.ToolPrefix` | `riscv32-wch-elf-` | 工具链前缀，一般不需改 |
| `EIDE.OpenOCD.ExePath` | `..\..\wch_flash\wch_openocd\bin\openocd.exe` | WCH OpenOCD 可执行文件，供 EIDE 下载/调试 |

说明：

- `.eide/eide.yml` 中的 `toolchainConfigMap.RISCV_GCC` 只保存编译选项（`arch: rv32imac`、`abi: ilp32`、`optimization: level-3`、`--specs=nano.specs`、`-lISP585 -lCH58xBLE` 等），**不含工具链的绝对路径**；路径由 EIDE 的工作区工具链设置提供，因此新环境必须在 EIDE 中重新选择/填写。
- 若编译报 `Cannot find riscv32-wch-elf-gcc` 或 `StdPeriphDriver / CH58xBLE` 链接失败，优先检查这里的工具链目录与库搜索路径。

### 3.2 `launch.json` —— "运行和调试"配置

**位置**：`.vscode/launch.json`（调试配置名 `Debug with OpenOCD`）。

> **该文件默认不随仓库同步**：`.gitignore` 中包含 `/.vscode/launch.json`，因为其中的调试器路径与个人环境绑定。克隆工程后 `.vscode/launch.json` 并不存在，需要在本地新建；若团队环境统一，也可以删除该忽略行（或 `git add -f .vscode/launch.json`）让它纳入版本管理。

**模板**（新建 `.vscode/launch.json` 后直接粘贴，再按下表改路径）：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug with OpenOCD",
            "type": "cortex-debug",
            "request": "launch",
            "cwd": "${workspaceFolder}",
            "executable": "./build/Debug/${workspaceFolderBasename}.elf",
            "servertype": "openocd",
            "serverpath": "${workspaceFolder}/../../wch_flash/wch_openocd/bin/openocd.exe",
            "toolchainPrefix": "${workspaceFolder}/../../Toolchain/RISC-V Embedded GCC15/bin/riscv32-wch-elf",
            "configFiles": [
                "${workspaceFolder}/../../wch_flash/wch_openocd/bin/wch-riscv.cfg"
            ],
            "svdFile": "./Drivers/Device/CH585.svd",
            "runToEntryPoint": "main",
            "showDevDebugOutput": "none",
            "liveWatch": {
                "enabled": true,
                "samplesPerSecond": 4
            }
        }
    ]
}
```

**需要按环境修改的字段：**

| 字段 | 模板中的值 | 说明 |
| --- | --- | --- |
| `serverpath` | `${workspaceFolder}/../../wch_flash/wch_openocd/bin/openocd.exe` | WCH OpenOCD 可执行文件 |
| `toolchainPrefix` | `${workspaceFolder}/../../Toolchain/RISC-V Embedded GCC15/bin/riscv32-wch-elf` | GDB 等工具前缀（GCC15） |
| `configFiles` | `${workspaceFolder}/../../wch_flash/wch_openocd/bin/wch-riscv.cfg` | OpenOCD 目标配置文件 |
| `executable` | `./build/Debug/${workspaceFolderBasename}.elf` | 若构建输出目录/文件名变化需同步 |
| `svdFile` | `./Drivers/Device/CH585.svd` | 外设寄存器视图；CH584 为 CH585 裁剪型号，**共用同一 SVD**，无需替换 |

前三项也可直接写成绝对路径，例如 `"serverpath": "C:/WCH/wch_openocd/bin/openocd.exe"`。

### 3.3 烧录命令 —— 自定义上传器命令行

**位置**：EIDE 项目设置 → **烧录配置**（Uploader 选 `Custom`）→ 命令行；
对应配置文件为 `.eide/eide.yml` 的 `targets.Debug.uploadConfigMap.Custom.commandLine`。

当前命令：

```bash
bash ../../wch_flash/wch_openocd.sh "${programFile}"
```

- 该命令包含**相对路径** `../../wch_flash/wch_openocd.sh`，迁移工程后必须改成实际的脚本路径。
- `"${programFile}"` 由 EIDE 自动替换为待烧录文件（当前 `Custom.bin` 为 `${ExecutableName}.elf`），一般无需改动。
- 该命令依赖 `bash`：Windows 下需保证 EIDE 调用命令所用的 Shell 能找到 Git Bash / MSYS2 的 `bash`；若不想用脚本，也可直接改为调用 `openocd.exe` 的参数形式。

**脚本 `wch_openocd.sh` 参考实现**（默认位于 `<WCH根>/wch_flash/wch_openocd.sh`）：

```bash
#!/bin/bash

firmwareFile="$1"

echo "--------------------------------------------------"
echo "firmware: ${firmwareFile}"
echo "--------------------------------------------------"

if [ ! -f "$firmwareFile" ]; then
    echo "ERROR: firmware not found"
    exit 1
fi

export FIRMWARE_FILE="$firmwareFile"

../../wch_flash/wch_openocd/bin/openocd \
    -f ../../wch_flash/wch_openocd/bin/wch-riscv.cfg \
    -c "init" \
    -c "halt" \
    -c "flash erase_sector wch_riscv 0 last" \
    -c 'program $::env(FIRMWARE_FILE)' \
    -c "wlink_reset_resume" \
    -c "exit"

exit $?
```

脚本要点：

| 语句 | 作用 |
| --- | --- |
| `firmwareFile="$1"` | 接收 EIDE 传入的 `${programFile}`（即 `build/Debug/xxx.elf`） |
| `[ ! -f "$firmwareFile" ]` | 文件不存在时报错并 `exit 1`，避免 OpenOCD 空跑 |
| `export FIRMWARE_FILE=...` + `$::env(FIRMWARE_FILE)` | 经环境变量把路径交给 OpenOCD 的 Tcl 解释器，避免路径含空格时被拆分 |
| `-f .../wch-riscv.cfg` | WCH 提供的 OpenOCD 目标配置（芯片/Flash 算法） |
| `init` → `halt` | 初始化调试器并暂停内核 |
| `flash erase_sector wch_riscv 0 last` | **整片擦除**（0 到最后一个扇区） |
| `program $::env(FIRMWARE_FILE)` | 烧录 ELF |
| `wlink_reset_resume` → `exit` | 复位并运行，然后退出 OpenOCD |

注意：

- **每次烧录都会整片擦除 Flash**（`0 last`）：SNV / 绑定信息以及 DataFlash 中的用户数据会一并丢失；需要保留时请把擦除范围改为仅代码区，或改用其它烧录方式。
- 脚本内部两处 `../../wch_flash/wch_openocd/bin/...` 同样是**相对路径**，与 3.1 / 3.2 的假设一致，迁移后要一并修改。
- 脚本中的 `openocd` 不带 `.exe` 后缀，由 bash（Git Bash / MSYS2）解析到 `openocd.exe`；若直接改用 `openocd.exe` 的等价命令行，注意 `program` 参数含空格时的引号处理。

---

## 4. 已知待实机验证的事项

> 寄存器地址与位域均取自数据手册，以下事项重点在于**驱动逻辑、配置参数与时序**是否适配实际硬件。

- [ ] 时钟树配置参数（时钟源、PLL、分频）与 `SystemInit` / `ch58x_hal_rcc` 的组合是否符合目标板需求
- [ ] GPIO 复用与方向配置（尤其是 BLE/RF 与 USB 相关引脚）
- [ ] UART / I2C / SPI / PWM / ADC / TIM 的初始化时序、分频与中断向量
- [ ] LCD 驱动与偏压配置
- [ ] BLE / RF / SNV 协议层与 `CH58xBLE` 库、ROM 符号（`ISP585.h`）的接口匹配
- [ ] `CH584_FLASH.ld` 的 Flash / RAM 布局与 `-lISP585` 链接结果
- [ ] 所用外设是否均在 CH584 上可用（CH584 为 CH585 裁剪型号，共用 SVD/库，但部分外设资源可能被裁减）
- [ ] 启动文件 `startup_CH585.S` 与中断入口 `ch58x_hal_it.c` 的符号是否一一对应

---

## 5. 使用 BLE / RF 库时的必要配置

BLE 与 RF-PHY 共用同一个 `libCH58xBLE.a`、同一套 TMOS、同一个 LLE 中断入口，因此**两者的宏开关、初始化底座与主循环要求完全相同**：都要定义 `CH58x_USE_BLE_LIB`，都要先调用 `HAL_STACK_Init()`，都要在主循环中反复调用 `HAL_STACK_Process()`。

### 5.1 需要启用的宏

宏位于 `Core/ch58x_hal_conf.h`：

```c
#define CH58x_USE_BLE_LIB
```

- **定义**：编译 `Protocol/ble`、`Protocol/rf`、`Protocol/stack`（BLE / RF / SNV）；
- **注释掉**：`Protocol/*.c` 因 `#ifdef` 全部编译为空，**不会产生编译 / 链接报错**，等同静默排除，无需从工程中移除；只有当应用代码仍 `#include` `Protocol/*.h` 时才会触发 `#error`（提示恢复宏）。

> 宏名带 `BLE_LIB`，但它控制的是整个 Protocol 层：**只用 RF、不用 BLE 时同样必须定义**。

同时确认：

- `CH58x_CHIP_MODEL` 与实际芯片一致（默认 `ID_CH584`）；
- 链接参数包含 `-lISP585 -lCH58xBLE`，库搜索路径包含 `Drivers/CH58x_HAL_Driver/LIB` 与 `.../LIB/BLE`（`.eide/eide.yml` 中已配置）。

### 5.2 初始化顺序与主循环

`HAL_STACK_Init()` 是 BLE 与 RF 的**共同底座**（内部执行 `BLE_LibInit()` 与 `TMOS_TimerInit()`），必须在任何 `BLE_*` / `RF_*` / `TMOS_*` / `GAPRole_*` 调用之前完成；`HAL_STACK_Process()` 内部即 `TMOS_SystemProcess()`，主循环中不可省略。

**BLE：**

```c
HAL_STACK_HandleTypeDef hstack = {0};
HAL_BLE_HandleTypeDef   hble   = {0};

hble.Init.AdvData    = advData;   /* 必须填，长度 1~31 */
hble.Init.AdvDataLen = sizeof(advData);

HAL_Init();              /* 1. 系统时钟与时基 */
HAL_STACK_Init(&hstack); /* 2. 协议栈底座 */
HAL_BLE_Init(&hble);     /* 3. 启动 GAP 角色 */

while (1) {
    HAL_STACK_Process(); /* 4. 不可省略 */
    /* 应用轮询代码放在其后（在空闲窗口执行） */
}
```

**RF-PHY（私有 2.4G 协议）：**

```c
HAL_STACK_HandleTypeDef hstack = {0};
RF_HandleTypeDef        hrf    = {0};

hstack.Init.CalibPeriodMs = HAL_STACK_CALIB_OFF; /* RF-only 必须关周期校准 */

hrf.Init.LLEMode       = LLE_MODE_PHY_1M; /* 白化 / PHY / 通道模式，按 rfConfig_t 语义 */
hrf.Init.Channel       = 37;              /* 数据通道 0~39 */
hrf.Init.accessAddress = 0x71764129;      /* 须满足 PHY 约束（见 5.4），收发双方一致 */
hrf.Init.CRCInit       = 0x555555;        /* 收发双方一致 */
hrf.Init.RxMaxlen      = 32;              /* 单包最大 251 */
hrf.Init.TxMaxlen      = 32;              /* 单包最大 251 */

/* 可选：先自检接入地址；非法值 HAL_RF_Init 会直接返回 HAL_ERROR */
if (HAL_RF_IsValidAccessAddress(hrf.Init.accessAddress) == 0U) {
    /* 处理错误 */
}

HAL_Init();              /* 1. 系统时钟与时基 */
HAL_STACK_Init(&hstack); /* 2. 协议栈底座 */
HAL_RF_Init(&hrf);       /* 3. 下发 rfConfig_t */

while (1) {
    HAL_STACK_Process(); /* 4. 不可省略 */
}
```

### 5.3 BLE 与 RF 的差异

| 项目 | BLE | RF-PHY |
| --- | --- | --- |
| 需要的目录 | `Protocol/stack` + `Protocol/ble` | `Protocol/stack` + `Protocol/rf`（`Protocol/ble` 可排除） |
| 初始化 | `HAL_STACK_Init()` → `HAL_BLE_Init()` | `HAL_STACK_Init()` → `HAL_RF_Init()` |
| 周期校准 `CalibPeriodMs` | 用默认值 `HAL_STACK_CALIB_PERIOD_MS`（120 s） | **必须**填 `HAL_STACK_CALIB_OFF` |
| SNV 绑定存储 | 可传 `HAL_SNV_*` | 五项全填 0 / NULL |
| 主循环 | 必须反复调用 `HAL_STACK_Process()` | 同左 |

要点：

- `HAL_STACK_Init()` 未成功就调用 BLE / RF 接口，GAPRole 与 TMOS 定时都会出错。
- 主循环中不调用 `HAL_STACK_Process()` 时，TMOS 任务与射频回调都不会被派发（表现为广播不启动、收发无回调）。
- `HAL_RF_Init()` / `HAL_RF_*_IT()` 须在 TMOS 任务上下文调用（库要求），用户回调也在模块任务上下文分发。
- `accessAddress` 必须满足 PHY 约束（见 5.4）；`HAL_RF_Init()` / `HAL_RF_SetConfig()` 遇到非法值直接返回 `HAL_ERROR`。
- `HAL_RCC_SYSCLK_HZ` 必须与实际系统时钟一致：RF 内部 2000 µs 事务等待系数按该宏在编译期换算，填错会导致超时判断失准。
- 运行期改 RF 参数：复制 `__HAL_RF_GET_CONFIG(&hrf)` 得到副本，修改后经 `HAL_RF_SetConfig()` 下发。
- BLE 与 RF-PHY 可以共存于同一工程，但 `rfConfig_t` 是单实例全局参数，同时启用时需自行确认不互相覆盖。
- 完全不使用 BLE / RF 时，直接注释掉 `CH58x_USE_BLE_LIB` 即可：`Protocol/*.c` 编译为空，不会报错；只要不再包含 `Protocol/*.h`，HAL 层不受影响。

### 5.4 accessAddress（AA）约束

`accessAddress` 是 PHY 层的 32 位接入地址，**收发双方必须完全一致**。它并非任意 32 位数都可用，`HAL_RF_IsValidAccessAddress()` 会逐项检查下面三条，任一不满足即判为非法：

| 约束 | 判定方式 | 不满足的后果 |
| --- | --- | --- |
| 禁止 `0x55555555` / `0xAAAAAAAA` | 与这两个值完全相等即非法 | 位模式直流不平衡，接收端无法稳定定界 |
| 相邻位反转次数 ≤ `HAL_RF_AA_MAX_TRANSITIONS`（24） | 统计相邻两位不同（0→1 / 1→0）的次数 | 跳变过多时同步头相关性反而下降 |
| 连续相同位长度 ≤ `HAL_RF_AA_MAX_RUN`（6） | 统计最长连续 0 或 1 的位数 | 连续 6 位以上相同时，接收端时钟恢复容易失锁 |

- 三条与 BLE Core Spec 对 Access Address 的要求一致。厂商 EVT（`BLE/RF_PHY/APP/RF_PHY.c`）的注释原文是“禁止使用 0x55555555 以及 0xAAAAAAAA（建议不超过 24 次位反转，且不超过连续的 6 个 0 或 1）”；本 HAL 把这三条都做成了**硬性校验**。
- 校验入口：
  - `HAL_RF_IsValidAccessAddress(aa)`：返回 `1` 合法 / `0` 非法，可在填写前自检；
  - `HAL_RF_Init()`：非法直接返回 `HAL_ERROR`（此时尚未注册 TMOS 任务，不会留下半初始化状态）；
  - `HAL_RF_SetConfig()`：运行期改配置同样校验，非法返回 `HAL_ERROR`，原配置保持不变。
- 厂商例程使用的合法值：`0x71764129`、`0x71762345`。

取值示例（按 32 位二进制逐位统计）：

| 地址 | 位反转次数 | 最长连续位 | 结果 |
| --- | --- | --- | --- |
| `0x71764129` | 17 | 5 | ✅ 合法（厂商例程值） |
| `0x71762345` | 17 | 3 | ✅ 合法（厂商例程值） |
| `0x55555555` | 31 | 1 | ❌ 禁用值且反转超限 |
| `0xFFFFFFFF` | 0 | 32 | ❌ 连续位超限 |

> 自检写法：`if (HAL_RF_IsValidAccessAddress(aa) == 0U) { /* 非法，重新取值 */ }`

---

## 6. 参考

- WCH 官网 CH585/CH584 产品页：<https://www.wch.cn/products/CH585.html>
- WCH 官方 SDK / EVT 示例（建议以其为功能正确性基准）
