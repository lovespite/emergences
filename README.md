# emergences · 涌现模拟

基于 C++20 和 Vulkan 的涌现模拟项目。项目按通用渲染层、通用模拟层和具体场景三层组织，首个场景采用 Boids 鸟群模型。完整方案见 [PLAN.md](PLAN.md)。

**Phase 1–3 已完成。** 默认应用运行真实的 CPU Boids 模拟，连接 Vulkan 粒子渲染、实时参数面板和光标吸引 / 排斥干预；保留轨道相机、地面网格、世界边界、窗口缩放重建和最小化暂停绘制。模拟、场景状态和干预库均可独立于 Vulkan 构建和测试。GPU compute 留待 Phase 4。

## 构建前置

- CMake 3.24 或更新版本。
- 支持 C++20 的编译器；下列预设使用 Visual Studio 2022、x64 目标和 MSVC 的 x64 宿主工具集。
- 构建应用时，需要完整的 LunarG Vulkan SDK，包括头文件、链接库和 `glslc`。显卡驱动附带的 Vulkan 运行时不足以编译项目。
- 首次配置需要联网下载固定版本的 GLFW、GLM 和 Dear ImGui。依赖版本及缓存说明见 [third_party/README.md](third_party/README.md)。

Phase 1 的历史验收记录（2026-10-22）：本机 SDK `1.4.363.0`（`D:\VulkanSDK\1.4.363.0`）的头文件、链接库、`glslc` 和 `VK_LAYER_KHRONOS_validation` 均可用。应用在 NVIDIA GeForce RTX 4060 Laptop GPU 上通过 Debug、Release 完整构建，当时两种配置的 CTest 均为 **3/3 通过**：`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外启用 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。模拟公共头文件独立编译和 `Agent` 布局检查也已通过。该记录不包含 Phase 2 新增的四项 CPU 测试。

Phase 2 验收记录（2026-10-22）：Debug / Release 全工程构建成功，两种配置的完整 CTest 均为 **7/7 通过**，包含 `sim_world_grid`、`sim_cpu`、`boids_rules`、`boids_simulation`、`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。独立 CPU 路径设置 `EMERGENCES_BUILD_APP=OFF`，在新目录 `build/sim-phase2` 完成 Debug / Release 构建，两种配置的 CPU 测试均为 **4/4 通过**；使用新目录是为了保留旧宿主工具集的 `build/sim-only` 缓存。

Phase 3 验收记录（2026-10-22）：Debug / Release 完整构建成功，完整 CTest 各 **10/10 通过**；Debug 开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。独立 CPU 路径在 `build/sim-phase3` 设置 `EMERGENCES_BUILD_APP=OFF`，Debug / Release 构建及测试各 **6/6 通过**。新增检查覆盖固定步长、暂停 / 单步 / 重置、着色、光标力和屏幕射线拾取；真实场景冒烟还对照未施力轨迹，验证两种干预实际改变运动。SDK 截图确认面板、密度色、圆点、吸引 / 排斥范围及真实鸟群聚集；截图与日志位于 `build-host-x64/verification-phase3/`。这些检查不包含逐项鼠标点击验收或正式性能基准。

CPU 融合与并行验收记录（2026-10-23）：Debug / Release 完整构建和 CTest 各 **12/12 通过**，Debug 开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断；关闭 OpenMP 的独立 CPU Release 构建 **8/8 通过**。六组固定快照在融合串行和默认自动并行下，单步及连续 32 步输出均与优化前逐字节一致。当前默认参数、本机 i9-13980HX、匹配正式应用编译选项的 Release 对照中，2048 个体演化 20 秒状态的单步约 **11.30→2.32 ms**，2048 极密集状态约 **83.12→10.82 ms**，8192 分团状态约 **107.93→20.48 ms**。数值为两轮中位数的汇总，每轮预热 2 次后测 11 次；恢复快照不计时，数据是热缓存单步耗时，不包含着色和呈现，不能直接换算为帧率。完整结果包含各轮波动，位于 `build/cpu-parallel-verification/production-summary.csv`；其余日志、固定快照对照程序和旧版源码也保存在该目录。默认应用已启用融合和自动线程策略，空间网格与显示颜色计算未在此次改动中优化。

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

- 数量 0–20000、暂停、暂停时单步、重置；Backend 当前为 CPU，GPU compute 选项灰显。
- **Behavior**：三条规则各自的权重 / 半径、最小 / 最大速度、最大加速度及 Wrap / Bounce 边界。权重设为 0 可关闭对应规则。
- **Behavior → Cursor force**：选择 Repel / Attract，设置作用半径、强度及 XZ 交互平面的高度。启用后，光标悬停在世界中的有效投影点即可施力；三个圆环显示球形作用范围。鼠标位于 UI、操作相机、离开窗口或窗口失焦时暂停施力。
- **View**：抗锯齿开关、箭头 / 圆点、大小、Direction / Speed / Density / Solid 四种颜色、地面网格与世界边界开关、相机重置。密度色使用半径 5 内的邻居数，0 为蓝色、32 及以上为橙红色，周期边界两侧邻居也计入。
- 左键拖动旋转、中键拖动平移、滚轮缩放；ImGui 捕获鼠标时，相机不响应相同输入。

**Anti-aliasing (MSAA)** 默认关闭，可在 View 面板或程序化预览面板随时切换。启用时优先使用 4× MSAA，不支持时使用 2×；若当前设备与附件格式都不支持多采样，开关灰显并继续使用单采样。箭头通过多采样平滑几何边缘，圆点通过 alpha-to-coverage 平滑圆周。切换会重建交换链附件及相关管线，保留模拟和相机状态。

场景以固定 `1/120` 秒推进。交互应用为每帧模拟设置 **8 ms 工作预算**，每完成一个完整步检查一次预算，单帧最多 8 步；超额时丢弃整步积压并保留不足一步的余量。单步不能中途打断，因此这不是硬性的 8 ms 帧耗时保证。高负载时模拟时间会放慢，面板显示减速提示、每帧耗时与步数，避免连续补算八个慢步拖垮界面。暂停不积累时间；重置保留当前数量、参数和暂停状态，同时清空模拟时钟并恢复确定性初始状态。

2026-10-22 针对「运行一段时间降至约 1 FPS」的性能回归使用当时的默认规则参数（`weight / radius`：Alignment `1 / 5`、Cohesion `0.6 / 5`、Separation `1.5 / 2`）：2048 个体演化 20 秒后，平均邻居数由 16.5 增至 304。已让空间查询缓存最短位移、复用最大半径邻居列表，消除规则中的重复周期计算，并加入上述交互预算。对保存的同一 20 秒状态，本机 Release 配对测量单步由约 **80.6 ms 降至 15.2 ms**；交互推进由八步约 **624 ms** 降至预算下的一步约 **14 ms**。后者包含模拟时钟减速，不能解读为维持相同实时推进速度下的帧率提升。8 个快照的一步输出，以及该组参数下连续 2400 步的 5 / 10 / 15 / 20 秒状态均与旧版逐字节一致。以上是旧默认参数的历史测量，不代表当前默认参数的性能。基准程序、快照与日志在 `build/perf-diagnosis/`；数值受主机负载影响，不承诺所有密度或数量下的帧率。

关闭窗口或按 Escape 即可退出。`--render-preview` 可打开原有程序化粒子预览。命令行提供三个独立的呈现测试：

```powershell
.\build-host-x64\Debug\emergences.exe --help
.\build-host-x64\Debug\emergences.exe --version
.\build-host-x64\Debug\emergences.exe --render-preview
.\build-host-x64\Debug\emergences.exe --smoke-test
.\build-host-x64\Debug\emergences.exe --render-smoke-test
.\build-host-x64\Debug\emergences.exe --boids-smoke-test
```

`--smoke-test` 保留原有渐变三角形测试：实际提交并呈现至少 12 帧，在第 4 和第 8 帧后调整窗口大小，确认至少 3 代交换链后退出。

`--render-smoke-test` 呈现至少 24 帧，依次使用 0 / 1 / 257 / 8192 / 96 / 2048 个程序化粒子，覆盖两个帧槽的空数组、扩容与缩减，以及箭头 / 圆点、颜色、动画暂停、网格 / 边界、相机和 ImGui 绘制；同样执行两次缩放并要求至少 3 代交换链，额外覆盖抗锯齿开启、关闭、再次开启，以及开启时缩放窗口，并检查实际采样数。

`--boids-smoke-test` 呈现至少 48 帧真实 CPU 模拟，依次覆盖 0 / 1 / 257 / 4096 / 96 / 512 / 2048 个体，检查运动、暂停 / 单步 / 重置，切换规则参数、速度限制、两种边界及四种颜色，并对照未施力轨迹验证吸引 / 排斥实际生效，同时检查禁止光标输入时力被关闭。末段使用 16 个密集团块，并以零工作预算确定性检查补算抑制及界面提示，不依赖机器速度断言。它执行两次窗口缩放并要求至少 3 代交换链，也覆盖抗锯齿开关及实际采样数检查。三个测试在未完成检查就关闭窗口时失败，Debug 下要求校验层可用，并在 API 校验错误时返回失败。冒烟测试注入固定光标坐标与输入条件，不模拟逐项鼠标点击或真实窗口焦点事件。

交互运行建议使用 Release：`cmake --build --preset release`，程序位于 `build-host-x64/Release/emergences.exe`。CPU 耗时会随个体数量、规则半径和局部密度增加，卡顿时可减少数量或半径。`vs2022` 预设固定使用 `build-host-x64` 构建目录和 x64 宿主工具集；不使用预设时，可配置同等目标：

```powershell
cmake -S . -B build-host-x64 -G "Visual Studio 17 2022" -A x64 -T host=x64
cmake --build build-host-x64 --config Debug
```

项目配置不硬编码本机 SDK 或编译器路径；Vulkan 由 CMake 的 `find_package(Vulkan)` 查找，着色器编译由构建期 `glslc` 命令完成。Windows 运行时从可执行文件旁的 `shaders/` 加载着色器，不依赖当前工作目录；移动程序时应一起复制该目录。

若已有构建缓存来自旧的宿主工具集，使用新的构建目录重新配置，避免在同一缓存中切换 toolset。

## 不依赖 Vulkan 的 CPU 模拟构建

尚未安装 SDK 时，可以单独构建 CPU 模拟与 Boids 规则。该路径仅需要 GLM，首次配置仍需下载依赖，不需要 Vulkan、GLFW 或 ImGui。

```powershell
cmake --preset vs2022-sim
cmake --build --preset sim-debug
ctest --preset sim-debug
cmake --build --preset sim-release
ctest --preset sim-release
```

`vs2022-sim` 使用独立目录 `build/sim-only` 并设置 `EMERGENCES_BUILD_APP=OFF`。`emergences::sim` 包含 World、SpatialHash、CpuSimulation 和可选的 RuleKernel 接口；`emergences::boids` 包含三规则、BoidsRuleKernel、BoidsScene、CursorIntervention / CursorRepel / CursorAttract，并传递链接模拟库。

`EMERGENCES_ENABLE_OPENMP` 默认 `ON`，配置时检测到编译器支持就启用 OpenMP，没有支持时自动使用串行路径。设为 `OFF` 可验证不依赖 OpenMP 的兼容构建；该配置仍使用 Boids 三规则融合计算：

```powershell
cmake -S . -B build/sim-serial -G "Visual Studio 17 2022" -A x64 -T host=x64 -DEMERGENCES_BUILD_APP=OFF -DEMERGENCES_ENABLE_OPENMP=OFF
cmake --build build/sim-serial --config Release
ctest --test-dir build/sim-serial -C Release --output-on-failure
```

启用 `BUILD_TESTING` 时，模拟层和场景库公共头文件分别独立编译，检查头文件自包含及 `Agent` 布局约束，同时注册以下八项 CPU 测试：

| CTest 名称 | 检查范围 |
| --- | --- |
| `sim_world_grid` | Wrap / Bounce 边界、最短周期位移、空间哈希与全量查询对照、半径与无重复邻居语义 |
| `sim_cpu` | 同步步进、规则加权、速度 / 加速度限幅、确定性初始化、重置 / 数量变化、干预和异常状态保持 |
| `sim_cpu_parallel` | 串行 / 并行轨迹与参数变化、通用规则串行回退、工作线程异常的确定性与状态保持、干预在并行结束后执行 |
| `boids_rules` | 三规则数学、独立半径、周期接缝、浮点半径边界、空邻域 / 重合 / 零半径及参数校验 |
| `boids_kernel` | 融合与独立规则逐分量位对照、规则顺序、参数快照、周期 / 半径边界、零参数、重合及不支持规则回退 |
| `boids_simulation` | 两种边界各 100 步全量邻域对照，以及 512 个体、240 步的确定性重放 |
| `boids_scene` | 60 / 120 / 144 Hz 帧时间分割一致性、步数上限、暂停 / 单步 / 重置、显示颜色不修改模拟、周期密度与固定色阶 |
| `boids_cursor` | 吸引 / 排斥方向、距离衰减、开关 / 零强度 / 零半径、周期接缝、非法输入与速度限制 |

这些测试也随完整应用构建注册。GPU 模拟尚未实现。

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
| `camera_math` | 相机默认取景、Vulkan 深度 / Y 轴约定、旋转 / 平移 / 缩放、屏幕射线与平面拾取、重置和极端输入 |
| `vulkan_triangle` | `--smoke-test` 的三角形呈现和交换链缩放 |
| `vulkan_particles` | `--render-smoke-test` 的粒子、辅助线、面板、相机和缓冲扩容路径 |
| `vulkan_boids` | `--boids-smoke-test` 的真实模拟上传、控制状态、光标力、四种颜色和缩放路径 |

三个 GPU 呈现测试会短暂创建窗口并自动缩放，测试的是渲染路径，当前模拟仍使用 CPU。CTest 会检查输出，在任何 `[Vulkan validation]` 消息（包括资源销毁时的消息）出现时失败；Loader 的一般诊断仍会输出。GPU 测试默认关闭，`camera_math` 在构建应用且启用 `BUILD_TESTING` 时注册。完整应用套件包含八项 CPU 模拟 / 场景测试与 `camera_math`，启用 GPU 测试后再加入三项呈现测试，合计十二项；单独模拟构建运行上述八项 CPU 测试。

需要额外复查同步时，可在当前 PowerShell 会话设置 `$env:VK_LAYER_VALIDATE_SYNC = '1'` 后运行 `ctest --preset debug`，完成后以 `Remove-Item Env:VK_LAYER_VALIDATE_SYNC` 清除该会话设置。

## 代码结构与接口约定

| 位置 | 当前职责 |
| --- | --- |
| `src/core/` | Window / Context；带逐图像深度附件的 Swapchain；共享命令记录、双帧同步与呈现的 FrameRenderer；Pipeline / Buffer / Descriptor；Camera、TriangleRenderer、ParticleRenderer、LineRenderer、ImGuiLayer |
| `src/sim/` | `Agent`、`World`、`Rule`、`Intervention`、`Simulation` 和后端枚举；`cpu/` 下为 SpatialHash、RuleKernel 接口与 CpuSimulation |
| `src/scenarios/boids/` | 纯 CPU 的三规则、BoidsRuleKernel、BoidsScene、光标干预及测试；LiveBoids 连接模拟 / 渲染 / 面板，保留程序化预览与三类呈现测试 |
| `shaders/` | `particle.*` 粒子、`grid.*` 辅助线及 `smoke/triangle.*` 三角形着色器 |
| `cmake/` | 依赖获取与着色器编译辅助配置 |
| `third_party/` | 第三方依赖说明；实际源码由 FetchContent 放入构建目录 |

`Agent` 使用 16 字节对齐、48 字节固定布局，包含 position / size、velocity / reserved、color / id 三组数据；字段偏移通过 `static_assert` 固定，以便后续与 GLSL `std430` 记录对应。应使用 GLM 默认的 packed `vec3`，不能直接启用会改变布局的 aligned 向量类型。

`Neighbor` 是 CPU 规则调用期间借用的邻居记录，使用 `double distanceSquared` 保留半径边界比较的精度，并可携带 `std::optional<glm::vec3> displacement`，复用同一 self / 邻居 / 世界下的最短位移。手工构造的记录可省略缓存，此时规则回退到 `World::displacement`；缓存只在查询借用期有效。它不属于 `Agent` 的 GPU 记录，不改变上述 48 字节布局。

渲染层使用独立的 `core::Particle` 记录和 `SceneView` 相机常量。场景将 CPU 显示记录转换为粒子数据，经各帧槽的 storage buffer 上传；顶点着色器通过 `gl_InstanceIndex` 读取，生成箭头或圆点 billboard。网格 / 边界使用静态顶点缓冲，光标作用范围由 ImGui 背景绘制列表显示为三个空间圆环。`FrameRenderer` 统一负责三角形、程序化预览与真实 Boids 的提交，交换链每幅图像各有深度附件，ImGui 的 Vulkan 资源在重建时重新配置。

Vulkan 实现参考：[描述符布局与缓冲](https://docs.vulkan.org/tutorial/latest/05_Uniform_buffers/00_Descriptor_set_layout_and_buffer.html)、[深度缓冲](https://docs.vulkan.org/tutorial/latest/07_Depth_buffering.html)。

模拟层不依赖 Vulkan。`Simulation::agents()` 返回借用的只读视图：CPU 后端提供个体数组，视图在下一次修改时失效；未来 GPU 后端返回空视图，不能隐式触发 GPU 回读。GPU 资源与同步桥接留待 Phase 4。规则和干预由模拟对象通过 `unique_ptr` 持有，仅在步进之间修改；干预仅适用于 CPU，未来 GPU 后端必须拒绝非空干预列表。

## 后续实现顺序

下一阶段为 **Phase 4**：GPU compute、资源与同步桥接及后端切换。Phase 3 的人工交互复查可按上述面板和相机操作进行。
