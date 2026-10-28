# emergences · 涌现模拟

基于 C++20 和 Vulkan 的涌现模拟项目。项目按通用渲染层、通用模拟层和具体场景三层组织，场景菜单提供 Boids 鸟群、Ant Colony 蚁群、Particle Life 与 Slime Mold 黏菌网络四个入口；蚁群包含独立的 2D 与 3D 模拟。完整方案见 [PLAN.md](PLAN.md)。

**Phase 1–4 已实现，并接入蚁群、Particle Life 与黏菌网络场景。** 默认应用运行 GPU compute Boids，可在面板切回 CPU 使用光标吸引 / 排斥，也可通过 **Scene** 菜单切换到蚁群、Particle Life 或 Slime Mold。Boids 两种后端共用规则参数、固定步长和显示控制。Boids GPU 使用均匀网格和计算着色器，显示结果直接绑定 Vulkan 粒子渲染；GPU 初始化失败时回退 CPU，并在面板显示原因。蚁群采用 CPU 模拟，支持随机地形障碍、随机食物、敌对生物追踪、觅食归巢与双信息素轨迹。Particle Life 通过可编辑的物种吸引／排斥矩阵模拟粒子聚集与追逐，默认优先 GPU，支持保留状态切换 CPU / GPU。Slime Mold 默认在 GPU 上执行感知、移动、沉积与轨迹扩散衰减，并直接绘制 GPU 轨迹场。模拟与场景库仍可独立于 Vulkan 构建和测试。

## 构建前置

- CMake 3.24 或更新版本。
- 支持 C++20 的编译器；下列预设使用 Visual Studio 2022、x64 目标和 MSVC 的 x64 宿主工具集。
- 构建应用时，需要完整的 LunarG Vulkan SDK，包括头文件、链接库和 `glslc`。显卡驱动附带的 Vulkan 运行时不足以编译项目。
- 首次配置需要联网下载固定版本的 GLFW、GLM 和 Dear ImGui。依赖版本及缓存说明见 [third_party/README.md](third_party/README.md)。

## macOS 26+ 构建与运行

安装支持 C++20 的 Apple Command Line Tools / Xcode 及 macOS SDK、CMake ≥ 3.24、Ninja，以及 [LunarG macOS Vulkan SDK](https://vulkan.lunarg.com/doc/view/latest/mac/getting_started.html)（含 `glslc`、Vulkan Loader 和 MoltenVK）。本机使用 Apple Clang 21、macOS SDK 27.0 和 Vulkan SDK 1.4.363.0；构建时将最低运行系统设为 macOS 26.0。OpenMP 为可选依赖，未检测到支持时 CPU 模拟自动使用串行路径。

在项目根目录运行 `build.sh`；若 SDK 未安装到系统搜索路径，先执行其 `setup-env.sh`，例如 `source "$HOME/VulkanSDK/1.4.363.0/setup-env.sh"`（路径按实际安装版本调整）：

```sh
./build.sh                                  # 默认构建 Release
./build.sh --configuration Debug --test      # 构建全部目标并运行无 GPU 测试
./build.sh --run                             # 构建成功后启动程序
./build.sh --jobs 4                          # 限制并行任务数（默认最多 8 个）
./build.sh --help                            # 查看参数说明
```

脚本支持从任意工作目录调用，兼容 macOS 自带 Bash。Release 产物位于 `build/macos-release/Release/`，Debug 产物位于 `build/macos-debug/Debug/`，目录内包含 `emergences`、`shaders/` 和 `emergences.conf.example`。重复运行会增量构建；默认只构建应用及其依赖，`--test` 构建全部目标并跳过带 `gpu` 标签的测试。构建或测试失败会立即停止并返回非零退出码；`--run` 在前面步骤成功后从产物目录启动程序，并等待程序退出。

也可手动执行等价的 CMake 构建，以下使用独立的 `build/macos-check` 目录：

```sh
cmake -S . -B build/macos-check -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build build/macos-check --parallel
./build/macos-check/Release/emergences
```

macOS 默认优先选择可用的 MoltenVK，并启用所需的 portability 扩展；启动日志显示实际驱动。需要指定驱动时，可用 Vulkan Loader 的 `VK_DRIVER_FILES` 指向对应 ICD JSON，例如：

```sh
VK_DRIVER_FILES="$HOME/VulkanSDK/1.4.363.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json" ./build/macos-check/Release/emergences
```

同目录的 `libkosmickrisp_icd.json` 可用于对照运行 KosmicKrisp。`VkResult -4` 表示运行时设备丢失（`VK_ERROR_DEVICE_LOST`）。本次优先 MoltenVK 是为了避开本机在 KosmicKrisp 下复现的 GPU 超时和设备丢失；这项对照尚不能单独证明根因属于驱动。

macOS 本机验收（2026-10-23，M1 Pro / macOS 26.6.2 / MoltenVK 1.4.2）：Debug、Release 各 22/22 项测试通过，包含 GPU 计算对照、呈现和场景切换；Debug 设置 `VK_LAYER_VALIDATE_SYNC=1`，无校验诊断。26,540 个体的配置副本在原 KosmicKrisp 路径约 20 秒复现设备丢失；改用 MoltenVK 后，Debug 连续运行 65 秒、默认驱动选择的 Release 连续运行 120 秒均未退出，随后由检查脚本结束进程。此记录是有限时长的本机验证，不代表无限时长稳定性保证。日志位于 `build/macos-debug/verification.log`、`build/macos-check/verification.log` 和 `build/macos-check/verification-device-loss/`。

## 一键生成 Windows 可执行程序

安装上述依赖，并在 Visual Studio 2022 / Build Tools 中安装“使用 C++ 的桌面开发”工作负载（含 Windows SDK）。将 CMake 加入 `PATH` 后，**双击项目根目录的 `build.bat`** 即可完成配置、编译和着色器生成，默认生成适合日常运行的 Release 版本：

```text
build-host-x64/Release/emergences.exe
build-host-x64/Release/shaders/
build-host-x64/Release/emergences.conf.example
```

构建结束后窗口会保留结果或错误信息；成功后可双击 `emergences.exe` 启动。脚本支持从任意工作目录调用，会尝试读取安装器保存的 `VULKAN_SDK` 环境变量，重复运行会增量构建。

在 PowerShell 中可使用以下命令；`build.bat` 带参数时不会暂停，适合终端和自动化调用：

```powershell
.\build.bat -Configuration Release             # 一键构建 Release
.\build.bat -Configuration Debug -Test         # 构建全部目标并运行无 GPU 测试
.\build.bat -Run                               # 构建 Release，成功后启动程序
.\build.bat -Jobs 4                            # 限制并行构建任务数（默认最多 8 个）

# 也可直接使用 PowerShell 脚本；支持 Windows PowerShell 5.1 和 PowerShell 7。
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
```

默认只构建应用及其依赖；`-Test` 构建全部目标并运行 CPU / 相机测试，即使已有缓存启用了 GPU 测试，也会跳过带 `gpu` 标签的测试。Debug 产物位于 `build-host-x64/Debug/`。构建或测试失败会立即停止并返回非零退出码；`-Run` 仅在前面步骤成功后启动程序，并等待程序退出。

这些脚本复用下面的 `vs2022`、`release` / `debug` CMake 预设，生成可执行文件和配套资源，不制作安装包。对外复制请使用 Release，将 `emergences.exe`、完整的 `shaders/` 目录和带逐项中文注释的 `emergences.conf.example` 一起分发；其他 Windows 电脑还需支持 Vulkan 的显卡驱动及 x64 Visual C++ 运行库（含 OpenMP 运行库）。

## 本机验收记录

蚁群接入验收（2026-10-23）：Debug / Release 完整构建成功，两种配置分别完成 13 项非 GPU 测试与 7 项 GPU / 呈现测试，均通过。新增 `ants_scene` 覆盖确定性、初始空信息素、沉积 / 扩散 / 挥发、固定步长与 60 秒觅食及食物守恒；`vulkan_ants` 覆盖实际呈现和场景切换。Debug 开启同步校验，无 Vulkan 校验诊断；SDK 截图确认食物、敌对生物、搬运状态与两类轨迹，截图位于 `build-host-x64/verification-ants/`。呈现检查通过程序驱动控件状态，未逐项执行鼠标点击验收。

Phase 1 的历史验收记录（2026-10-22）：本机 SDK `1.4.363.0`（`D:\VulkanSDK\1.4.363.0`）的头文件、链接库、`glslc` 和 `VK_LAYER_KHRONOS_validation` 均可用。应用在 NVIDIA GeForce RTX 4060 Laptop GPU 上通过 Debug、Release 完整构建，当时两种配置的 CTest 均为 **3/3 通过**：`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外启用 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。模拟公共头文件独立编译和 `Agent` 布局检查也已通过。该记录不包含 Phase 2 新增的四项 CPU 测试。

Phase 2 验收记录（2026-10-22）：Debug / Release 全工程构建成功，两种配置的完整 CTest 均为 **7/7 通过**，包含 `sim_world_grid`、`sim_cpu`、`boids_rules`、`boids_simulation`、`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。独立 CPU 路径设置 `EMERGENCES_BUILD_APP=OFF`，在新目录 `build/sim-phase2` 完成 Debug / Release 构建，两种配置的 CPU 测试均为 **4/4 通过**；使用新目录是为了保留旧宿主工具集的 `build/sim-only` 缓存。

Phase 3 验收记录（2026-10-22）：Debug / Release 完整构建成功，完整 CTest 各 **10/10 通过**；Debug 开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。独立 CPU 路径在 `build/sim-phase3` 设置 `EMERGENCES_BUILD_APP=OFF`，Debug / Release 构建及测试各 **6/6 通过**。新增检查覆盖固定步长、暂停 / 单步 / 重置、着色、光标力和屏幕射线拾取；真实场景冒烟还对照未施力轨迹，验证两种干预实际改变运动。SDK 截图确认面板、密度色、圆点、吸引 / 排斥范围及真实鸟群聚集；截图与日志位于 `build-host-x64/verification-phase3/`。这些检查不包含逐项鼠标点击验收或正式性能基准。

CPU 融合与并行验收记录（2026-10-23）：Debug / Release 完整构建和 CTest 各 **12/12 通过**，Debug 开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断；关闭 OpenMP 的独立 CPU Release 构建 **8/8 通过**。六组固定快照在融合串行和默认自动并行下，单步及连续 32 步输出均与优化前逐字节一致。当前默认参数、本机 i9-13980HX、匹配正式应用编译选项的 Release 对照中，2048 个体演化 20 秒状态的单步约 **11.30→2.32 ms**，2048 极密集状态约 **83.12→10.82 ms**，8192 分团状态约 **107.93→20.48 ms**。数值为两轮中位数的汇总，每轮预热 2 次后测 11 次；恢复快照不计时，数据是热缓存单步耗时，不包含着色和呈现，不能直接换算为帧率。完整结果包含各轮波动，位于 `build/cpu-parallel-verification/production-summary.csv`；其余日志、固定快照对照程序和旧版源码也保存在该目录。默认应用已启用融合和自动线程策略，空间网格与显示颜色计算未在此次改动中优化。

Phase 4 验收记录（2026-10-23）：Debug / Release 完整构建及 CTest 各 **15/15 通过**；Debug 启用 `VK_LAYER_VALIDATE_SYNC=1`，无校验诊断。最终默认 GPU 启动与计算调整后，Debug 的后端契约、GPU 场景、GPU 计算三项复测也全部通过。独立无 Vulkan、关闭 OpenMP 的 `build/phase4-scene-tests` 在 Debug / Release 各 **9/9 通过**。CPU/GPU 对照覆盖两种边界、周期接缝、重合点、零半径 / 零权重、513 个体拥挤单格和完整 32³ 网格；本轮最大位置误差小于 `8e-6`，速度误差小于 `4e-6`。10 万个体状态、速度限制与 ID 完整性通过，SDK 截图确认 GPU 面板、密度色圆点和箭头绘制。测试日志和截图在 `build-host-x64/verification-phase4/`，未进行逐项鼠标点击验收。

10 万个体在本机 RTX 4060 Laptop GPU 的 Release 冒烟采样约 **37.12 ms/步**：默认规则、均匀初始分布，预热 1 步后连续 3 步取平均，计入提交及 fence 等待，不含着色和呈现。这只是短时运行检查，运行间波动明显，不是正式性能基准或帧率承诺。

SDK 截图层已验证箭头、圆点和缩放后的实际画面，本机产物位于 `build-host-x64/verification-phase1/`。冒烟测试通过程序设置切换模式和相机，并绘制面板；这些检查不包含鼠标逐项点击面板的 UI 自动化。

本机 Vulkan Loader 仍报告两个不存在的 Steam 隐式图层 JSON 路径。这是已有系统注册项的加载诊断，不影响此次呈现；程序保留 `[Vulkan loader]` 输出，与 `[Vulkan validation]` 的 API 校验消息区分，未修改系统注册表。

## 配置、构建与运行

在项目根目录打开 PowerShell。安装 Vulkan SDK 后，让当前终端中的 `VULKAN_SDK` 指向实际 SDK 根目录；若安装器已设置环境变量，重新打开终端即可。

```powershell
# 已打开的终端可从安装器注册的环境变量加载 SDK，无需重启电脑。
$env:VULKAN_SDK = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
if (-not $env:VULKAN_SDK) {
    $env:VULKAN_SDK = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'User')
}

cmake --preset vs2022
cmake --build --preset debug
.\build-host-x64\Debug\emergences.exe
```

默认在 `[-40,40]` 的三维世界中运行 2048 个真实 Boids。面板提供以下操作：

- 数量 0–100000、暂停、暂停时单步、重置；Backend 可切换 CPU / GPU compute。切换保留个体位置、速度、ID、规则、世界、速度限制、暂停状态与模拟时钟。
- **Behavior**：三条规则各自的权重 / 半径、最小 / 最大速度、最大加速度及 Wrap / Bounce 边界。权重设为 0 可关闭对应规则。
- **Behavior → Cursor force**：CPU 后端可选择 Repel / Attract，设置作用半径、强度及 XZ 交互平面的高度。启用后，光标悬停在世界中的有效投影点即可施力；三个圆环显示球形作用范围。鼠标位于 UI、操作相机、离开窗口或窗口失焦时暂停施力。GPU 后端禁用光标力，切回 CPU 后保留控件设置。
- **View**：抗锯齿开关、箭头 / 圆点、大小、Direction / Speed / Density / Solid 四种颜色、地面网格与世界边界开关、相机重置。密度色使用半径 5 内的邻居数，0 为蓝色、32 及以上为橙红色，周期边界两侧邻居也计入。
- 左键拖动旋转、中键拖动平移、滚轮缩放；ImGui 捕获鼠标时，相机不响应相同输入。
- **F11** 或面板顶部的 **Fullscreen (F11)** 开关切换全屏；使用窗口当前所在的显示器，退出全屏后恢复窗口位置、尺寸及最大化状态。
- **F1** 显示 / 隐藏控制面板，隐藏后模拟和相机操作继续，再按一次即可恢复。全屏开关和 F1 同样适用于 `--render-preview`。

### 黏菌网络场景（Slime Mold）

在 **Scene → Slime Mold** 切换，或运行 `./build/macos-debug/Debug/emergences --slime-mold`（Windows：`emergences.exe --slime-mold`）。默认 GPU、30000 个体、5 个营养源，在 100×100 的 XZ 周期平面上生成不断重组的轨迹网络。`--no-config` 可忽略历史设置。场景设置保存在 `scenarios.slime_mold.*`，启动场景名为 `app.scenario = slime_mold`。

- **Behavior**：速度、探针距离/夹角、转向、游走、沉积、扩散、挥发和营养吸引强度；支持 0–100000 个体。
- **World**：0–12 个持续营养源、种子及重新生成；0 个营养源用于观察自由网络。改变个体数、种子或营养源数量会重建世界、清空轨迹和时钟。
- **View**：轨迹亮度、个体叠加、营养标记、抗锯齿与相机重置。亮度和显示开关不改变模型状态。
- CPU / GPU 切换保留位置、方向、轨迹场、参数、种子、暂停状态和完整步数；暂停支持单步。切到其他场景后黏菌停止演化，返回继续。GPU 初始化失败时回退 CPU 并显示原因。

采用受 [Jeff Jones 的 Physarum 粒子模型](https://uwe-repository.worktribe.com/output/980579/characteristics-of-pattern-formation-and-evolution-in-approximations-of-physarum-transport-networks) 启发的简化趋化模型：前、左、右三个探针读取旧轨迹和营养场，个体转向移动并沉积；随后对全场执行周期九点均值扩散和指数衰减。营养源是不会被消耗的固定吸引点。此实现允许个体重叠，未实现生物生长、排他占位或最短路径求解，不声称精确复现原论文。

`src/scenarios/slime_mold/` 中的场景和 GPU 适配器不依赖 Vulkan 类型，复用 `sim::ComputeDevice`。`shaders/compute/slime_mold.comp` 的 clear → sense/move/deposit → diffuse/decay 各阶段由现有设备接口同步；个体与轨迹均使用双缓冲，32 位整数原子累加沉积（精度 1/4096，合法参数下即使十万个体集中一格也不会溢出），浓度上限 1000。显示阶段生成 GPU 粒子缓冲，用现有渲染器新增的方格模式拼成连续轨迹图。普通演化、调参和显示不回读位置或轨迹，只有显式快照和切回 CPU 才下载；计算提交仍采用框架现有的同步 fence 路径。复杂度为 O(个体数 + 网格格数)，不执行个体两两邻域查询。

CPU 与 GPU 采用相同种子、逐个体/逐步整数哈希和固定 60 Hz 步长；浮点感知分支可放大细微数值差异，不保证长时间轨迹逐位相同。测试覆盖质量守恒、周期接缝、探针转向、沉积竞争、浓度上限、状态传输、失败时资源保留及直接 GPU 渲染：

```sh
cmake --build build/macos-debug --parallel 6
VK_LAYER_VALIDATE_SYNC=1 ctest --test-dir build/macos-debug -R slime_mold --output-on-failure
# GPU 测试须在配置时启用 -DEMERGENCES_ENABLE_GPU_TESTS=ON；CPU 模型也可独立构建。
```

本机验收（2026-10-25，Apple M1 Pro / MoltenVK）：Debug 与 Release 的 `config_settings`、`slime_mold_scene`、`slime_mold_backend`、`vulkan_slime_mold`、`slime_mold_gpu_compute` 各 5/5 通过；无 Vulkan Release 构建中的配置和黏菌测试 3/3 通过。Debug 其余 10 项 GPU / 窗口 / 呈现回归全部通过，GPU 检查开启同步校验，无 Vulkan 校验诊断。默认 30000 个体额外演化 60 秒模拟时间，导出轨迹图确认网络形成，浓度有限且保持范围；数据与图像位于 `build/slime-mold-verification/`。Release 的 23 项非 GPU 回归在修复 CPU 扩散索引并专项复测后各项通过。Debug 非 GPU 回归中原有 `ants3d_scene` / `ants3d_terrain` 分别触发 180 / 30 秒超时，其 Release 版本通过；未修改这些测试的超时。Mac 锁屏期间无法执行手动鼠标验收，呈现检查通过程序驱动控件完成。

### 蚁群场景

在面板顶部选择 **Scene → Ant Colony → Dimensions → 2D plane**，或使用 `emergences.exe --ants` 直接启动 2D 蚁群。**Dimensions** 与 Particle Life 使用相同的下拉切换方式，可选择 **2D plane / 3D terrain**。两种蚁群分别保留个体、参数、暂停状态与模拟时钟，切换维度不会重新生成世界，未显示的模拟停止推进；本次运行中还分别保留相机视角，从其他场景返回 **Ant Colony** 时恢复上次使用的维度。

蚁群默认包含 **1500 只蚂蚁**，数量可调至 0–20000，采用 CPU 固定步长模拟。世界位于 `[-80,80]` 的 XZ 平面，尺寸为 **160×160**，长宽均为原来的两倍；信息素使用 256×256 网格，保持原有空间精度。蚁巢在中央；种子生成六片食物和三个敌对生物。敌对生物每 **0.5 秒**重新评估 **40 单位**范围内的蚁群，以半径 **6 单位**的邻域统计密度，优先追踪密集区域，并通过 **A\*** 寻路绕过石块。无目标时继续游走，蚁巢周围半径 **9 单位**保留保护范围。蚂蚁探索、拾取食物并送回蚁巢，接近敌对生物时转向避让；当前敌对生物只影响行进路线，没有战斗或死亡机制。

随机地形默认包含 **24 个石块障碍**，数量可调至 0–48；设为 0 可恢复开阔地面。石块大小与位置随世界种子生成，半径范围为 **2–7 单位**，最大半径由 4.5 增至 7；相邻石块按可用空间调整大小，始终保留至少 **4 单位**通道，并避开蚁巢、食物及敌对生物出生点。蚂蚁和敌对生物会转向绕行，碰撞约束阻止穿越石块，信息素也不会在障碍内部沉积或穿过障碍扩散。灰色石块的外沿对应实际碰撞范围。

食物默认每 **10 个模拟秒**随机补充两片，间隔可调至 1–120 秒。补充时优先复用已耗尽的食物槽，最多保留 24 片；达到上限时跳过多余的生成，保留仍有余量的食物。暂停或切换到其他场景时停止计时；改变补充间隔或重置世界后重新计时，相同种子与参数可复现补充过程。

**信息素初始为空。** 外出探索的蚂蚁留下归巢信息素，携带食物的蚂蚁留下食物信息素；两种轨迹逐步扩散、挥发，蚂蚁通过前方和左右方向的采样寻找路径。面板提供以下控制：

- **Paused / Step / Reset**：暂停、暂停时单步、按当前种子恢复初始世界。重置保留数量、行为参数和暂停状态，并清空模拟时钟、搬运统计与信息素。
- **World**：`World seed` 输入框是待应用的草稿；点击 **Generate from seed** 才生成并保存该种子的地形、食物与敌对生物。**Random world** 选择新种子并立即生成世界。**Obstacles** 调整石块数量；改变障碍或蚂蚁数量会按当前种子重新开始。**Food interval (s)** 调整食物补充间隔，并显示下次补充倒计时和当前食物片数 / 24。
- **Behavior**：调整移动速度、传感距离 / 角度、转向、探索扰动，以及信息素沉积、挥发、扩散；**Clear pheromones** 只清除两种轨迹，保留当前世界和时钟。
- **View**：分别显示蓝色归巢信息素和绿色食物信息素，调整蚂蚁大小、抗锯齿、网格 / 边界及相机。面板同时显示携带食物的蚂蚁数、已送达食物、剩余食物和敌对生物数量。

### 3D 蚁群场景

选择 **Scene → Ant Colony → Dimensions → 3D terrain**，或运行 `emergences --ants-3d`。这是独立的 CPU 模拟：与原有 2D 蚁群分别保存数量、种子、暂停状态、食物、轨迹与模拟时钟；切走后停止推进，切回后继续。`--ants` 仍直接启动 2D 蚁群，两种维度在 UI 中统一使用 **Ant Colony** 入口。

3D 地图覆盖 `[-128,128]` 的 XZ 平面，尺寸由 **96×96** 扩大到 **256×256**，面积约为原来的 **7.1 倍**。地图参考 [Minecraft 的种子与梯度噪声生成思路](https://learn.microsoft.com/en-us/minecraft/creator/documents/world-generation?view=minecraft-bedrock-stable)，通过多频率 [Perlin 噪声](https://mrl.cs.nyu.edu/~perlin/noise/)叠加宽阔起伏、山脊与局部细节，并按地形高度和坡度着色。本实现采用连续实体三角网格，只借鉴生成思路，不复刻 Minecraft 的完整体素世界算法。每个世界种子生成不同的山丘、洞穴位置、蚁巢和食物布局，相同种子与参数可复现初始世界。初始八片食物分布在蚁巢附近及较远的探索区域。

洞穴由 **四个大小不同的洞室、弯曲通道、侧向分支和三个洞口** 组成，上方覆盖不规则山体。洞穴位置随世界种子在较大范围内随机选取，避开水域、桥梁与地图边界，并为附近蚁巢保留可通行空间。洞壁、洞顶、洞口及世界边缘参与球体碰撞。关闭 **Show cave roofs** 可剖开山体与高处洞顶，保留低矮洞壁以观察内部布局。

每个种子还会生成贯穿地图的弯曲河流、**两座桥** 和 **三个水坑**。河流与水坑不能直接穿越，桥面与两岸坡道连通；蚂蚁探索时根据附近可行走地面绕开障碍，搬运食物时使用共享的地形寻路图返回蚁巢。食物只放在蚁巢可达的位置，水面、河床、桥面与碰撞范围随种子一起重建。

蚂蚁出巢后独立随机探索，不预先知道食物位置。只有进入食物中心 **10 个世界单位**以内，且与食物之间存在直接可行走路径时，才会被吸引并靠近拾取；洞壁、洞顶及水域会阻断这种感知。食物耗尽或超出感知距离后，蚂蚁恢复探索。每只蚂蚁的探索使用独立随机序列，远处尚未发现的食物不会改变其行进路线。

默认 **600 只**，可调至 0–3000。每只蚂蚁有半径 **0.28** 的球形碰撞体，空间网格筛选邻近个体，再在三维空间解算接触，避免穿过地形和彼此重叠。渲染使用带头、胸、腹、六条腿和触角的立体网格，并随地面法线倾斜。模型的腿和触角为视觉细节，物理解算使用统一球体。

蚂蚁统一为**黑色**，搬运食物时上方显示一个小绿点。初始个体在巢内隐藏排队，依次出巢；蚁巢的出口和入口默认各一个，可分别配置为 **1–4 个**，每个通道一次允许一只通过，多个通道可同时运行。送达食物后，个体进入 **5–30 个模拟秒**的随机冷却，期间不渲染、不参与碰撞，也不可点击；冷却结束后重新加入出巢队列。暂停或切换场景时冷却停止计时，同一世界种子可复现排队和冷却过程。

- **Movement speed**：觅食速度默认 **4.7 世界单位 / 秒**，可调至 **0.5–20**，即时生效；搬运和出入巢速度同比例变化，冷却和食物生成计时保持不变。
- **World → Nest exits / Nest entrances**：分别设置出口和入口数量。修改后按当前种子重新开始蚁群，清除选择与跟随，并更新蚁巢边缘的开口。
- **World → Food spawn interval**：每隔 **1–120 个模拟秒**生成最多两片可达食物，默认 **10 秒**；食物点最多 **24 片**，优先复用耗尽的位置槽位。修改间隔会从当前时刻重新计时；暂停和切换到其他场景时停止计时，Reset 恢复最初八片食物。
- **World → World seed**：输入种子后点击 **Generate from seed** 重建地图和蚁群；**Random map** 选择新种子并立即生成。生成后切回全图视角，清空已有轨迹、搬运统计与模拟时钟，保留数量、速度、通道数量、食物生成间隔和暂停状态。
- **Collision → Show collision volumes**：显示青色碰撞球轮廓。
- **Show cave roofs**：关闭后剖开洞顶，观察洞内蚂蚁；隐藏洞顶不改变物理碰撞。
- **View**：轨迹、抗锯齿及全图、蚁巢、洞口、洞顶剖视、洞室内部和河流桥梁相机预设。点击场景后，**WASD** 按相机朝向在水平方向自由移动，**Q / E** 降低 / 升高，**Shift** 加速，**左键或右键拖动** 以相机自身为中心原地转头；中键平移、滚轮缩放。左键单击仍用于选择个体。编辑种子或操作面板时不会触发键盘移动。
- **选择与跟随**：左键单击蚂蚁显示金色选中圆环，按 **Space** 开始跟随；未选中时，自动选择最近完成出巢且仍在外活动的蚂蚁。没有符合条件的个体时保持原视角。按 **F2** 切换第一、第三人称，也可在未跟随时预选模式。第一人称保持水平地平线，平滑眼高和转向，隐藏自身模型；第三人称从后上方跟随并显示自身模型，遇到地形遮挡时收近镜头。再次按 **Space** 或 **Esc** 返回进入跟随前的视角。跟随期间暂停自由相机操作；个体入巢冷却时自动退出跟随，保留选中状态以显示冷却倒计时。重置、改数量、改出入口数量或生成地图会清除选择。
- **Paused / Step / Reset**：暂停、120 Hz 单步及按当前种子重放初始世界；调整数量也会按当前种子重新开始。

3D 设置写入独立的 `scenarios.ants3d.*` 配置项，新增 `movement_speed`、`nest_exit_count`、`nest_entry_count` 和 `food_spawn_interval`；`scenarios.ants3d.seed` 同时保存地图与蚁群的世界种子，场景值为 `app.scenario = ants3d`。重启按种子重新生成初始世界，不保存当前个体位置或搬运进度。`--ants-3d-smoke-test`（或 `--ants-3d-smoke`）执行实体网格呈现、两种跟随视角、自动选择最新出巢个体、洞顶剖视、碰撞体显示、暂停／单步／重置、2D／3D 切换及 MSAA / 全屏检查；CPU 测试单独验证地形、个体碰撞、出入巢通道和定时食物生成。

### Particle Life 场景

在面板顶部选择 **Scene → Particle Life**，或以 `emergences --particle-life`（Windows 使用 `emergences.exe`）直接启动。使用 **`emergences --particle-life-3d`** 可直接进入 GPU 三维模拟；面板的 **Dimensions → 2D plane / 3D volume** 也可随时切换。默认 **2000 个粒子、6 个物种**，支持 0–10000 个粒子和 1–8 个物种。2D 使用 **100×100** 的 XZ 平面；3D 使用 **100×100×100** 的体积，粒子可沿三个方向运动，六个面均为周期边界，坐标范围为 **[-50,50)**。默认优先使用 **GPU compute**，初始化失败自动回退 CPU，并在面板显示原因；两种后端均使用 **120 Hz 固定步长**。

CPU 通过空间哈希查询周期邻域；GPU 在显存中构建三维空间网格并查询相邻格子，按作用半径精确筛选，不截断邻居数量。2D 和 3D 均复用这一路径。粒子状态和邻域构建留在 GPU，状态缓冲直接交给 Vulkan 粒子渲染，无每帧状态回读；只有切换到 CPU 或显式测试才读取状态。CPU / GPU 的浮点累计精度和顺序不同，数值验证采用容差，长期轨迹不保证逐位相同。大量粒子挤入同一局部区域时，邻居数量仍可能显著增加。

3D 默认使用斜视轨道相机和立方体边界，圆点增加球面明暗辅助判断深度。左键拖动旋转、中键拖动平移、滚轮缩放；**View → Reset camera** 恢复当前维度的视角，F1 可隐藏控制面板。同样数量的粒子放进体积后邻居更稀疏，因此 `--particle-life-3d` 在未保存作用半径时采用 **22**；已有半径会保留，面板 **Behavior → Use 3D starter radius (22)** 可主动应用这个起点。三维形态取决于密度和作用矩阵，旋转视角有助于观察团簇内部及前后关系。

所有粒子在近距离互相排斥，避免塌缩；在其余作用半径内，物种矩阵决定吸引或排斥，强度随距离呈三角形变化，超出半径后不再作用。矩阵的**行是受力物种、列是施力物种**，正值吸引、负值排斥；矩阵不必对称，因此可以出现一个物种追逐另一个物种的运动。模型参考 [Particle Life 官方框架的力函数说明](https://particle-life.com/java-framework/getting-started.html)。默认矩阵保留同类吸引，便于观察聚集；具体形态会随参数和矩阵变化。

- **Backend**：切换 CPU / GPU compute，保留粒子位置、速度、物种、规则矩阵、参数、暂停状态和已完成的模拟步数；切换仅丢弃不足一个固定步长的时间余量。
- **Dimensions**：切换 2D / 3D 会按当前种子重新生成粒子并重置时间，保留后端、规则矩阵、参数和暂停状态，同时调整观察相机。
- **Paused / Step / Reset**：暂停、单步、按当前种子恢复初始粒子；重置保留规则矩阵、参数和暂停状态。
- 调整粒子数、物种数、种子，以及作用半径、作用强度、阻尼、近距排斥、核心距离比例和速度上限。数量、物种数和种子变化会重新生成粒子。
- 彩色作用矩阵可逐格编辑，并可随机生成新的非对称规则；更改规则会作用于当前粒子。物种颜色同时用于矩阵和渲染。
- 粒子大小、网格、边界、抗锯齿和相机均可调；切换到其他场景时冻结当前模拟，切回来继续。

设置保存为 `app.scenario = particle_life` 和 `scenarios.particle_life.*`，包括所有 8×8 矩阵值；`scenarios.particle_life.dimensions = 2d`（默认）或 `3d` 记忆维度，`scenarios.particle_life.backend = gpu`（默认）或 `cpu` 记忆后端偏好。旧配置保持 2D。初始化失败的自动回退不会覆盖 GPU 偏好，面板切换成功后才保存所选后端；`--particle-life-3d` 明确选择 3D 和 GPU。重启恢复参数及规则，从种子重新生成初始粒子。呈现回归可使用 `emergences --particle-life-smoke-test`，覆盖 2D / 3D 往返切换、GPU 的 Y 方向运动与周期边界、GPU 直接绘制、CPU / GPU 状态切换、粒子数量、物种数、矩阵修改、暂停／单步／重置、三个场景切换、帧预算、窗口大小、全屏、F1 和 MSAA。

性能取决于粒子分布、作用半径和设备。开启 `EMERGENCES_ENABLE_GPU_TESTS` 并构建 Release 后，可运行计算测试的基准模式（以下为本机单配置构建路径）。GPU 单步耗时包含提交与同步等待，不含状态上传、回读或渲染，不能直接换算为应用帧率：

```sh
build/macos-check/src/scenarios/boids/emergences_particle_life_gpu_tests --benchmark
```

在 **Apple M1 Pro / MoltenVK 1.4.2 / Release** 上，3D 作用半径 **22** 的单步测量如下。每组运行六次，丢弃首轮预热，取后五次的中位数；每次测量恢复相同初态。

| 3D 分布 | 粒子数 | CPU 单步 | GPU 单步 |
| --- | ---: | ---: | ---: |
| 均匀 | 2000 | 9.378 ms | 1.980 ms |
| 均匀 | 10000 | 228.713 ms | 10.498 ms |
| 密集 | 2000 | 16.610 ms | 4.827 ms |
| 密集 | 10000 | 399.224 ms | 15.490 ms |

GPU 耗时包含计算提交与同步等待，不含上传、回读和渲染。120 Hz 是模拟固定步长，不保证所有粒子数量与密度都能按实时速度完成每步；工作超出帧预算时会放慢模拟以保持控制响应。

### 配置文件与设置记忆

交互启动时自动读取**可执行文件旁的 `emergences.conf`**，与启动时的工作目录无关。首次运行会生成完整配置；面板、快捷键和相机操作产生的设置修改每秒检查一次，有变化才写入，正常退出时再保存一次。下次启动恢复当前场景、Boids 参数、Boids 与 Particle Life 的 CPU / GPU 选择、蚁群参数和已应用种子、Particle Life 参数及作用矩阵、暂停、光标力、全屏、面板显隐、形状、颜色、大小、抗锯齿、网格 / 边界及相机位置。记忆的是设置，个体位置、食物消耗、信息素与模拟时间仍从初始状态开始。

文件采用点路径与 `key = value` 格式，例如：

```ini
app.scenario = ants
scenarios.boids.cohesion_weight = 0.11
scenarios.boids.min_speed = 8
scenarios.boids.max_speed = 14
scenarios.boids.backend = gpu
scenarios.boids.cursor.enabled = off
scenarios.ants.count = 1500
scenarios.ants.obstacle_count = 24
scenarios.ants.seed = 12345
scenarios.ants.food_spawn_interval = 10
scenarios.ants.deposit_rate = 4
scenarios.ants.show_home_pheromone = on
scenarios.ants.show_food_pheromone = on
scenarios.particle_life.backend = gpu
scenarios.particle_life.dimensions = 3d
scenarios.particle_life.count = 2000
view.fullscreen = on
view.agent_shape = arrow
view.color = direction
view.agent_size = 0.45
view.panel_visible = on
```

支持空行、`#` / `;` 注释（包括行尾注释）、UTF-8 BOM 和 Windows 换行。布尔值使用 `on` / `off`；场景 `app.scenario` 为 `boids` / `ants` / `particle_life` / `ants3d`，两种蚁群与 Particle Life 设置分别位于 `scenarios.ants.*`、`scenarios.ants3d.*` 和 `scenarios.particle_life.*`，其种子范围均为 0–2147483647。形状为 `arrow` / `dot`，颜色为 `direction` / `speed` / `density` / `solid`，边界 `scenarios.boids.boundary` 为 `wrap` / `bounce`。可编辑项及范围与面板一致；相机位于 `view.camera.*`，角度单位为弧度。可参考 [emergences.conf.example](emergences.conf.example)，或直接编辑首次运行生成的完整文件。

构建应用时会将 `emergences.conf.example` 同步到可执行文件旁，Debug / Release 均包含。示例逐项解释用途、取值范围、默认值和相关行为；只修改示例注释后再次构建也会更新副本。程序不会自动加载 `.example` 文件；要使用其示例值，可在关闭程序后复制为 `emergences.conf`，或复制为另一配置文件并用 `--config` 指定。构建不会覆盖个人的 `emergences.conf`。

建议关闭程序后编辑配置再启动，运行中的界面修改会自动写回同一文件。缺省项目沿用默认值；错误行、无效枚举、越界或非有限数值会给出带行号的提示并使用有效值 / 默认值，最小与最大速度在全部行解析完成后联合检查。注释和未知配置项在保存时保留。写入使用同目录临时文件替换原文件；无法读取或保存时在面板和终端报告，模拟仍可继续。

```powershell
.\build-host-x64\Release\emergences.exe --config D:\Profiles\flock.conf
.\build-host-x64\Release\emergences.exe --no-config
.\build-host-x64\Release\emergences.exe --ants
```

`--config` 指定要读取和记忆的文件，相对路径以当前工作目录为基准，父目录需已存在。`--no-config` 仅使用默认值，不读写配置。所有冒烟测试也不读写配置。程序化预览共用全屏、形状、抗锯齿、网格 / 边界、面板显隐和相机设置，预览的数量、动画、大小及颜色单独保存在 `scenarios.preview.*`。删除配置文件后重新启动即可恢复默认设置；构建不会覆盖已有配置。

Windows 版本启动时不创建控制台窗口。双击程序直接显示应用；从已有终端启动时保留日志输出，命令行帮助、版本和测试的重定向输出仍可使用。没有错误输出通道的交互启动失败会显示错误对话框。

**Anti-aliasing (MSAA)** 默认开启，可在 View 面板或程序化预览面板随时切换。启用时优先使用 4× MSAA，不支持时使用 2×；若当前设备与附件格式都不支持多采样，开关灰显并继续使用单采样。箭头通过多采样平滑几何边缘，圆点通过 alpha-to-coverage 平滑圆周。切换会重建交换链附件及相关管线，保留模拟和相机状态。

场景以固定 `1/120` 秒推进。交互应用为每帧模拟设置 **8 ms 工作预算**，每完成一个完整步检查一次预算，单帧最多 8 步；超额时丢弃整步积压并保留不足一步的余量。单步不能中途打断，因此这不是硬性的 8 ms 帧耗时保证。高负载时模拟时间会放慢，面板显示减速提示、每帧耗时与步数，避免连续补算八个慢步拖垮界面。暂停不积累时间；重置保留当前数量、参数和暂停状态，同时清空模拟时钟并恢复确定性初始状态。

2026-10-22 针对「运行一段时间降至约 1 FPS」的性能回归使用当时的默认规则参数（`weight / radius`：Alignment `1 / 5`、Cohesion `0.6 / 5`、Separation `1.5 / 2`）：2048 个体演化 20 秒后，平均邻居数由 16.5 增至 304。已让空间查询缓存最短位移、复用最大半径邻居列表，消除规则中的重复周期计算，并加入上述交互预算。对保存的同一 20 秒状态，本机 Release 配对测量单步由约 **80.6 ms 降至 15.2 ms**；交互推进由八步约 **624 ms** 降至预算下的一步约 **14 ms**。后者包含模拟时钟减速，不能解读为维持相同实时推进速度下的帧率提升。8 个快照的一步输出，以及该组参数下连续 2400 步的 5 / 10 / 15 / 20 秒状态均与旧版逐字节一致。以上是旧默认参数的历史测量，不代表当前默认参数的性能。基准程序、快照与日志在 `build/perf-diagnosis/`；数值受主机负载影响，不承诺所有密度或数量下的帧率。

关闭窗口或按 Escape 即可退出。`--render-preview` 可打开原有程序化粒子预览。命令行提供六个独立的呈现测试：

```powershell
.\build-host-x64\Debug\emergences.exe --help
.\build-host-x64\Debug\emergences.exe --version
.\build-host-x64\Debug\emergences.exe --render-preview
.\build-host-x64\Debug\emergences.exe --smoke-test
.\build-host-x64\Debug\emergences.exe --render-smoke-test
.\build-host-x64\Debug\emergences.exe --boids-smoke-test
.\build-host-x64\Debug\emergences.exe --gpu-smoke-test
.\build-host-x64\Debug\emergences.exe --ants-smoke-test
.\build-host-x64\Debug\emergences.exe --particle-life-smoke-test
```

`--smoke-test` 保留原有渐变三角形测试：实际提交并呈现至少 12 帧，在第 4 和第 8 帧后调整窗口大小，确认至少 3 代交换链后退出。

`--render-smoke-test` 呈现至少 24 帧，依次使用 0 / 1 / 257 / 8192 / 96 / 2048 个程序化粒子，覆盖两个帧槽的空数组、扩容与缩减，以及箭头 / 圆点、颜色、动画暂停、网格 / 边界、相机和 ImGui 绘制；同样执行两次缩放并要求至少 3 代交换链，额外覆盖抗锯齿开启、关闭、再次开启，以及开启时缩放窗口，并检查实际采样数。

粒子预览、CPU / GPU Boids 呈现测试还覆盖全屏进入 / 退出、F1 隐藏 / 恢复及按住不重复切换，检查隐藏期间没有面板绘制命令。`window_fullscreen` 桌面测试独立检查全屏窗口恢复、F11 按键与 ImGui 回调衔接；这些测试需开启 `EMERGENCES_ENABLE_GPU_TESTS` 并在有桌面的环境运行。

`--boids-smoke-test` 呈现至少 48 帧真实 CPU 模拟，依次覆盖 0 / 1 / 257 / 4096 / 96 / 512 / 2048 个体，检查运动、暂停 / 单步 / 重置，切换规则参数、速度限制、两种边界及四种颜色，并对照未施力轨迹验证吸引 / 排斥实际生效，同时检查禁止光标输入时力被关闭。末段使用 16 个密集团块，并以零工作预算确定性检查补算抑制及界面提示，不依赖机器速度断言。它执行两次窗口缩放并要求至少 3 代交换链，也覆盖抗锯齿开关及实际采样数检查。上述 CPU 呈现测试在未完成检查就关闭窗口时失败，Debug 下要求校验层可用，并在 API 校验错误时返回失败。冒烟测试注入固定光标坐标与输入条件，不模拟逐项鼠标点击或真实窗口焦点事件。

`--ants-smoke-test` 呈现至少 48 帧蚁群场景，覆盖 0 / 1 / 257 / 1500 只蚂蚁、0 / 24 / 48 个障碍、食物和敌对生物绘制、运动与自然信息素沉积、暂停 / 单步 / 重置、种子生成、清除 / 隐藏轨迹、帧预算及 Boids / 蚁群切换，并检查障碍外沿与碰撞范围一致、未显示的蚁群没有继续推进。同样覆盖窗口缩放、抗锯齿、全屏与面板显隐；长时间觅食、食物守恒及确定性重放由独立的 `ants_scene` 测试检查。

交互运行建议使用 Release：`cmake --build --preset release`，程序位于 `build-host-x64/Release/emergences.exe`。CPU 耗时会随个体数量、规则半径和局部密度增加，卡顿时可减少数量或半径。`vs2022` 预设固定使用 `build-host-x64` 构建目录和 x64 宿主工具集；不使用预设时，可配置同等目标：

```powershell
cmake -S . -B build-host-x64 -G "Visual Studio 17 2022" -A x64 -T host=x64
cmake --build build-host-x64 --config Debug
```

项目配置不硬编码本机 SDK 或编译器路径；Vulkan 由 CMake 的 `find_package(Vulkan)` 查找，着色器编译由构建期 `glslc` 命令完成。Windows 运行时从可执行文件旁的 `shaders/` 加载着色器，不依赖当前工作目录；移动程序时应一起复制该目录。

若已有构建缓存来自旧的宿主工具集，使用新的构建目录重新配置，避免在同一缓存中切换 toolset。

## 不依赖 Vulkan 的 CPU 模拟构建

尚未安装 SDK 时，可以单独构建 CPU 模拟、Boids 规则、蚁群与 Particle Life 场景。该路径仅需要 GLM，首次配置仍需下载依赖，不需要 Vulkan、GLFW 或 ImGui。

```powershell
cmake --preset vs2022-sim
cmake --build --preset sim-debug
ctest --preset sim-debug
cmake --build --preset sim-release
ctest --preset sim-release
```

`vs2022-sim` 使用独立目录 `build/sim-only` 并设置 `EMERGENCES_BUILD_APP=OFF`。`emergences::sim` 包含 World、SpatialHash、CpuSimulation、GpuSimulation、ComputeDevice 和可选的 RuleKernel 接口；`emergences::boids` 包含三规则、BoidsRuleKernel、BoidsGpu、BoidsScene、CursorIntervention / CursorRepel / CursorAttract；`emergences::ants` 包含纯 CPU 的 AntScene、食物 / 敌对生物状态与双信息素场；`emergences::particle_life` 包含 ParticleLifeScene 和仅依赖 ComputeDevice 抽象接口的 ParticleLifeGpu。`emergences::ants3d` 包含独立的 Ant3DScene、山丘与洞穴 Terrain 及球体碰撞。四个场景库均传递链接模拟库，CPU 模拟与后端契约测试不需要 Vulkan。

`EMERGENCES_ENABLE_OPENMP` 默认 `ON`，配置时检测到编译器支持就启用 OpenMP，没有支持时自动使用串行路径。设为 `OFF` 可验证不依赖 OpenMP 的兼容构建；该配置仍使用 Boids 三规则融合计算：

```powershell
cmake -S . -B build/sim-serial -G "Visual Studio 17 2022" -A x64 -T host=x64 -DEMERGENCES_BUILD_APP=OFF -DEMERGENCES_ENABLE_OPENMP=OFF
cmake --build build/sim-serial --config Release
ctest --test-dir build/sim-serial -C Release --output-on-failure
```

启用 `BUILD_TESTING` 时，模拟层和场景库公共头文件分别独立编译，检查头文件自包含及 `Agent` 布局约束，同时注册以下十六项无 Vulkan 测试：

| CTest 名称 | 检查范围 |
| --- | --- |
| `config_settings` | 点路径语法、默认值、非法值、速度区间、Unicode 路径、完整保存 / 重读、注释保留和读写失败保护 |
| `sim_world_grid` | Wrap / Bounce 边界、最短周期位移、空间哈希与全量查询对照、半径与无重复邻居语义 |
| `sim_cpu` | 同步步进、规则加权、速度 / 加速度限幅、确定性初始化、重置 / 数量变化、干预和异常状态保持 |
| `sim_cpu_parallel` | 串行 / 并行轨迹与参数变化、通用规则串行回退、工作线程异常的确定性与状态保持、干预在并行结束后执行 |
| `boids_rules` | 三规则数学、独立半径、周期接缝、浮点半径边界、空邻域 / 重合 / 零半径及参数校验 |
| `boids_kernel` | 融合与独立规则逐分量位对照、规则顺序、参数快照、周期 / 半径边界、零参数、重合及不支持规则回退 |
| `boids_simulation` | 两种边界各 100 步全量邻域对照，以及 512 个体、240 步的确定性重放 |
| `boids_scene` | 60 / 120 / 144 Hz 帧时间分割一致性、步数上限、暂停 / 单步 / 重置、显示颜色不修改模拟、周期密度与固定色阶 |
| `boids_cursor` | 吸引 / 排斥方向、距离衰减、开关 / 零强度 / 零半径、周期接缝、非法输入与速度限制 |
| `boids_backend` | 状态 / 参数 / 规则所有权迁移、时钟 / 暂停、GPU 重置、无隐式回读、传输失败保留活动状态 |
| `boids_config` | 配置应用后的初始速度与重置、参数采集、规则顺序、零个体及活动 GPU 参数无回读 |
| `ants_scene` | 种子生成与确定性重放、随机障碍与碰撞、空初始信息素、固定步长 / 暂停 / 单步 / 重置 / 预算、数量与参数边界、定时随机补充食物与容量上限、信息素扩散 / 挥发与障碍阻隔、长时间觅食与食物守恒、平面边界 |
| `enemy_pathfinder` | A* 绕障、体积与边界间距、斜向通行安全、精确端点连接、巢穴保护、不可达判定与确定性 |
| `enemy_tracking` | 密集蚁群优先、感知范围、目标移动与消失、重置后重放、绕过石块及巢穴保护区 |
| `particle_life_scene` | 有向矩阵、分段力函数、周期邻域、同步积分、确定性初始化、固定步长 / 暂停 / 单步 / 重置、参数边界与状态导入 |
| `particle_life_backend` | CPU / GPU 状态迁移、时钟与暂停保留、GPU 重置与参数更新、无隐式回读、传输失败保留活动状态 |

这些测试也随完整应用构建注册。`boids_backend` 和 `particle_life_backend` 使用内存计算设备验证后端转移契约，不需要 Vulkan。

## 使用 CPU Boids 库

项目内的 CMake 目标链接 `emergences::boids`，即可使用以下最小组装方式：

```cpp
#include <memory>

#include "sim/cpu/CpuSimulation.h"
#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/BoidsRuleKernel.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

int main() {
    using namespace emergences;
    sim::cpu::CpuSimulation simulation(512, sim::World{}, 1234U);
    simulation.rules().push_back(std::make_unique<scenarios::boids::Alignment>());
    simulation.rules().push_back(std::make_unique<scenarios::boids::Cohesion>());
    simulation.rules().push_back(std::make_unique<scenarios::boids::Separation>());
    simulation.setRuleKernel(std::make_unique<scenarios::boids::BoidsRuleKernel>());
    simulation.setWorkerCount(0); // 默认自动选择；设为 1 可固定串行。
    simulation.setMotionLimits({1.0F, 8.0F, 20.0F});

    for (int step = 0; step < 240; ++step) {
        simulation.step(1.0F / 120.0F);
    }
    // simulation.agents() 提供只读状态，可由场景层转换为渲染记录。
}
```

`BoidsRuleKernel` 在一次邻居遍历中累加三规则，保留各规则的浮点精度、邻居顺序和原规则加权顺序。它支持 Alignment、Cohesion、Separation 各一个的任意排列；每步在主线程重新读取权重 / 半径，生成工作线程只读的参数快照。未安装 kernel，或集合含自定义、缺失、重复规则时，CPU 后端使用通用串行规则路径。模拟层只依赖 `RuleKernel` 抽象，不依赖 Boids 类型；本次优化保持原有 `SpatialHash` 网格实现。

`setWorkerCount(0)` 为默认自动策略：少于 512 个体时串行，其余取运行时可见逻辑处理器数的一半，至少 1 个、至多 16 个线程，并受 OpenMP 运行时上限和个体数量限制。`setWorkerCount(1)` 固定串行；大于 1 表示请求线程数，仍受运行时、逻辑处理器数和个体数量限制。只有支持当前规则集合的 kernel 才允许并行，没有 OpenMP 时始终串行。`workerCount()` 返回配置值而非某一步实际使用的线程数。线程数、kernel 和规则集合只在步进之间修改；空间网格重建、干预和最终状态校验仍串行执行。

`World` 默认是 `[-25,25]` 的周期世界（`BoundaryMode::Wrap`），应用的 `BoidsScene` 显式使用 `[-40,40]` 以匹配渲染边界。两者都支持 `Bounce` 硬墙反弹和越过多次边界的位移。周期邻居距离、聚拢和分离均使用最短周期方向，因此两侧接缝附近的个体相互可见。世界范围必须有限且各轴跨度为可表示的正浮点数。

`SpatialHash` 每步按启用规则的最大半径重建稀疏均匀网格。通常每轴格宽至少为最大半径，查询相邻至多 27 格；若某轴世界跨度小于半径，则该轴仅使用一格。周期查询去重、按数组索引排除自身，最后以精确距离筛选。每条规则再按自己的半径筛选，包含恰好位于半径边界的邻居，半径为零时邻域为空。网格减少普通分布下的候选数量，但密集群体或大半径仍可退化为近似全量两两比较，不保证线性最坏复杂度。

三规则的默认 `weight / radius` 分别是 Alignment `0.45 / 5`、Cohesion `0.11 / 6`、Separation `2 / 2`。Alignment 返回邻居平均速度减自身速度；Cohesion 返回邻居最短位移的平均值；Separation 平均累加 `-displacement / max(distanceSquared, 1e-6)`，精确重合时跳过无方向的邻居。规则仅返回未加权加速度，后端乘权重一次，零权重禁用该规则。参数构造与修改均拒绝负数和非有限值。

每一步的所有规则读取同一份旧状态，后端合并并限制加速度，再按半隐式 Euler 更新速度、限制速度范围、更新位置并处理边界。默认最小 / 最大速度是 `1 / 8`，最大加速度是 `20`，可通过 `setMotionLimits` 调整；零速度需要提升到最小速度时使用由种子和 ID 决定的方向。`step(dt)` 接受有限非负秒数，`dt=0` 不推进；建议以固定步长运行，并由应用累积帧时间，避免帧率变化改变模拟轨迹。

初始化由种子和索引确定。`reset()` 按当前数量、世界和速度限制重新生成初始状态；`setCount()` 缩减时保留前缀，增加时保留现有个体并确定性地生成追加个体。`setAgents()` 可导入状态且保留提供的 ID；ID 是元数据，不是邻域数组索引，导入与追加组合使用时不自动保证唯一。`setWorld()`、`setMotionLimits()` 和状态导入会重新规范速度与边界。

干预在积分后按插入顺序执行，可以修改个体数量、状态和世界。全部干预结束后，后端校验最终状态、重施速度限制与边界，再提交新状态；失败的步进保留外部可见的旧个体和世界，但无法撤销回调对这些记录以外的副作用。规则与干预集合只能在步进之间修改，不能包含空指针。`CursorRepel` / `CursorAttract` 是独立于 UI 的 CPU 干预：使用世界最短位移计算球形邻域，在作用半径内按 reverse smoothstep 平滑衰减，以强度乘 `dt` 修改速度；之后仍遵守后端速度限制。

`BoidsScene` 组装三规则、自动安装 `BoidsRuleKernel`，按上述默认策略启用支持的多线程计算，并管理固定步长、模拟时钟和显示颜色。`simulation()` 暴露底层 CPU 参数接口；`advance(elapsedSeconds, workBudgetSeconds)` 拒绝负数和非法时间，工作预算默认正无穷，保留无墙钟限制的确定性推进；交互应用显式传入 `0.008`。`lastAdvanceLimited()` 表示最近一次推进丢弃了时间，`singleStep()` 只在暂停时推进一步。`refreshColors(ColorMode)` 生成独立显示记录，方向、速度、密度与纯色切换不会修改模拟状态。密度查询复用 `SpatialHash`，使用固定邻居数色阶，避免远处个体数量变化导致局部颜色重新缩放。

## 可选 GPU 呈现测试

安装 SDK 且有可用的 Vulkan 显卡和桌面环境后，可启用 CTest 中的呈现测试：

```powershell
cmake --preset vs2022 -DEMERGENCES_ENABLE_GPU_TESTS=ON
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
ctest --test-dir build-host-x64 -C Release --output-on-failure
```

| CTest 名称 | 检查范围 |
| --- | --- |
| `camera_math` | 相机默认取景、Vulkan 深度 / Y 轴约定、旋转 / 平移 / 缩放、屏幕射线与平面拾取、状态恢复、重置和极端输入 |
| `window_fullscreen` | 全屏进入 / 退出、窗口状态恢复、F11 与 ImGui 回调衔接 |
| `vulkan_triangle` | `--smoke-test` 的三角形呈现和交换链缩放 |
| `vulkan_particles` | `--render-smoke-test` 的粒子、辅助线、面板、相机和缓冲扩容路径 |
| `vulkan_boids` | `--boids-smoke-test` 的真实模拟上传、控制状态、光标力、四种颜色和缩放路径 |
| `vulkan_gpu_boids` | `--gpu-smoke-test` 的 GPU 直接绘制、0 / 1 / 257 / 100000 个体、后端切换、暂停 / 单步 / 重置、颜色、边界、缩放和 MSAA |
| `vulkan_ants` | `--ants-smoke-test` 的蚁群 / 地形 / 食物 / 敌对生物 / 信息素绘制、数量、障碍碰撞范围显示、暂停 / 单步 / 重置、种子、预算与场景切换 |
| `vulkan_particle_life` | `--particle-life-smoke-test` 的 2D / 3D 往返切换、GPU Y 运动与周期边界、GPU 直接绘制、CPU / GPU 切换、数量与物种、矩阵、种子、暂停 / 单步 / 重置、预算、场景切换与窗口控制 |
| `boids_gpu_compute` | 真实计算设备上的 CPU/GPU 规则对照、网格与边界、状态读回和十万个体计算检查 |
| `particle_life_gpu_compute` | 真实计算设备上的 CPU / GPU 单步与连续步对照、周期边界、重合点、非对称矩阵、速度限制与状态切换；另提供 `--benchmark` 模式 |

六个呈现测试会短暂创建窗口并自动缩放；窗口测试和计算对照测试也需要可用的桌面和 Vulkan 设备。CTest 在任何 `[Vulkan validation]` 消息（包括资源销毁时的消息）出现时失败；Loader 的一般诊断仍会输出。GPU 测试默认关闭。完整应用构建注册 23 项非 GPU 测试、`window_fullscreen`、8 项呈现测试和 3 项 GPU 计算测试，合计 35 项；单独模拟构建（`EMERGENCES_BUILD_APP=OFF`）运行 21 项测试。此处列出当前注册项，历史验收记录仍对应各自当时的实现与测试数量。

需要额外复查同步时，可在当前 PowerShell 会话设置 `$env:VK_LAYER_VALIDATE_SYNC = '1'` 后运行 `ctest --preset debug`，完成后以 `Remove-Item Env:VK_LAYER_VALIDATE_SYNC` 清除该会话设置。

## 代码结构与接口约定

| 位置 | 当前职责 |
| --- | --- |
| `src/config/` | 独立于 Vulkan 的点路径配置解析、校验与原子保存；支持设置记忆和注释保留 |
| `src/core/` | Window / Context；带逐图像深度附件的 Swapchain；共享命令记录、双帧同步与呈现的 FrameRenderer；Pipeline / Buffer / Descriptor、VulkanComputeDevice；Camera、TriangleRenderer、ParticleRenderer、MeshRenderer、LineRenderer、ImGuiLayer |
| `src/sim/` | `Agent`、`World`、`Rule`、`Intervention`、`Simulation`、`MotionLimits`、`ComputeDevice`；`cpu/` 下为 SpatialHash、RuleKernel 与 CpuSimulation，`gpu/` 下为 GpuSimulation |
| `src/scenarios/boids/` | 纯 CPU 的三规则、BoidsRuleKernel、BoidsScene、光标干预及测试；LiveBoids 管理 Boids / 2D 蚁群 / 3D 蚁群 / Particle Life 切换并连接模拟 / 渲染 / 面板，保留程序化预览与呈现测试 |
| `src/scenarios/ants/` | 无 Vulkan 依赖的 AntScene、双信息素场、随机地形、食物和敌对生物及测试；AntPresentation 负责蚁群面板、设置转换和渲染记录 |
| `src/scenarios/ants3d/` | 独立的 Ant3DScene、带山丘和拱形洞穴的 Terrain、球体碰撞与空间网格、实体模型及面板；CPU 地形与碰撞测试 |
| `src/scenarios/particle_life/` | 无 Vulkan 类型依赖的 ParticleLifeScene 与 ParticleLifeGpu，提供 CPU 周期邻域、GPU 状态管理、分段作用力及测试；ParticleLifePresentation 负责后端与作用矩阵面板、设置转换和 CPU 渲染记录 |
| `src/scenarios/slime_mold/` | 独立二维黏菌场景、CPU 对照与 GPU 驻留轨迹/个体、参数面板及后端/计算测试 |
| `shaders/` | `particle.*` 粒子、`grid.*` 辅助线、`smoke/triangle.*` 三角形，以及 `compute/boids.comp` / `compute/particle_life.comp` 空间网格计算着色器和 `compute/slime_mold.comp` 轨迹场计算着色器 |
| `cmake/` | 依赖获取与着色器编译辅助配置 |
| `third_party/` | 第三方依赖说明；实际源码由 FetchContent 放入构建目录 |

`Agent` 使用 16 字节对齐、48 字节固定布局，包含 position / size、velocity / reserved、color / id 三组数据；字段偏移通过 `static_assert` 固定，以便后续与 GLSL `std430` 记录对应。应使用 GLM 默认的 packed `vec3`，不能直接启用会改变布局的 aligned 向量类型。

`Neighbor` 是 CPU 规则调用期间借用的邻居记录，使用 `double distanceSquared` 保留半径边界比较的精度，并可携带 `std::optional<glm::vec3> displacement`，复用同一 self / 邻居 / 世界下的最短位移。手工构造的记录可省略缓存，此时规则回退到 `World::displacement`；缓存只在查询借用期有效。它不属于 `Agent` 的 GPU 记录，不改变上述 48 字节布局。

渲染层使用独立的 `core::Particle` 记录和 `SceneView` 相机常量。场景将 CPU 显示记录转换为粒子数据，经各帧槽的 storage buffer 上传；GPU 场景直接提供兼容布局的 storage buffer。顶点着色器通过 `gl_InstanceIndex` 读取，生成箭头、圆点或蚂蚁形状。网格 / 边界使用静态顶点缓冲，光标作用范围由 ImGui 背景绘制列表显示为三个空间圆环。`FrameRenderer` 统一负责三角形、程序化预览、Boids、蚁群与 Particle Life 的提交，交换链每幅图像各有深度附件，ImGui 的 Vulkan 资源在重建时重新配置。

Vulkan 实现参考：[描述符布局与缓冲](https://docs.vulkan.org/tutorial/latest/05_Uniform_buffers/00_Descriptor_set_layout_and_buffer.html)、[深度缓冲](https://docs.vulkan.org/tutorial/latest/07_Depth_buffering.html)。

模拟层不依赖 Vulkan。`Simulation::agents()` 返回借用的只读视图：CPU 后端提供个体数组，视图在下一次修改时失效；GPU 后端始终返回空视图。`readback()` 是显式状态快照接口，GPU 会同步回读。世界、速度限制和状态导入通过共同的 `Simulation` 接口配置。规则和干预由模拟对象通过 `unique_ptr` 持有，仅在步进之间修改；GPU 拒绝非空干预列表。

## GPU compute 与后端切换

`sim::ComputeDevice` 仅暴露不透明缓冲 / 管线句柄、上传、显式下载和批量 dispatch。`core::VulkanComputeDevice` 在同时支持 graphics / compute 的同一队列提交计算，使用设备本地 SSBO、传输暂存缓冲、阶段间内存屏障和 fence。当前每批计算同步等待完成，便于安全复用双份模拟状态和显示缓冲；尚未实现计算与图形异步重叠。

`GpuSimulation` 不识别 Boids C++ 类型，由场景的 `BoidsGpu` 适配器编码规则。`shaders/compute/boids.comp` 按格计数、分块前缀和、散射得到连续的 cell→agent 索引区间，再查询相邻至多 27 格并按各规则半径精确过滤。周期相邻格去重；每格容量没有固定截断。三规则 GLSL 与 C++ 数学对应，浮点累计顺序及精度不同，因此跨后端采用数值容差比较，不承诺长期轨迹逐位相同。

GPU 步进使用旧状态读取、新状态写入的双缓冲。四种显示颜色写入独立 GPU 显示缓冲，`ParticleRenderer::drawBuffer` 直接读取该 SSBO，不经过 CPU 数组。改变显示颜色不修改模拟状态。后端切换、数量变化和世界 / 速度限制变化允许显式回读；日常演化与着色不回读。

`BoidsScene::currentSimulation()` 返回当前后端；原有 `simulation()` 保留 CPU 具体接口，GPU 活跃时不代表当前状态。`setBackend()` 在状态传输成功后提交切换，并保留暂停与模拟时钟；CPU 干预对象留在 CPU 后端。两种后端都保留固定步长与交互预算。均匀网格能减少普通分布的候选数量，但高密度或极大半径仍可退化到近似全量两两比较；十万个体的短时测试不代表所有密度下都能实时运行。

`ParticleLifeGpu` 使用独立的计算着色器，在 GPU 内按格计数、计算前缀和并散射粒子索引（count / scan / scatter），建立连续的 cell→particle 索引区间。演化线程按格内索引顺序分配，使空间上相邻的粒子接近执行；每个粒子仅查询相邻格子，再按作用半径与周期最短距离精确筛选。2D 的 Y 轴使用单格，3D 使用完整三轴网格；周期相邻格去重，每格和每粒子的邻居数量均无固定截断。作用力使用补偿浮点求和减小与 CPU 双精度累计的误差。模拟双缓冲本身即可供 `ParticleRenderer::drawBuffer` 绘制；物种标签留在 CPU 供面板统计，位置和速度在 GPU 演化期间不回读。`ParticleLifeScene::agents()` 在 GPU 模式返回空视图，只有显式 `readback()` 或切换到 CPU 才下载状态；降低速度上限也由 GPU 计算完成。网格可减少一般分布的候选邻居，高密度团簇仍可能退化到接近全量两两比较。
