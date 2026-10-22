# emergences · 涌现模拟

基于 C++20 和 Vulkan 的涌现模拟项目。项目按通用渲染层、通用模拟层和具体场景三层组织，首个场景采用 Boids 鸟群模型。完整方案见 [PLAN.md](PLAN.md)。

**Phase 1 的 Vulkan 渲染框架与 Phase 2 的 CPU 模拟、Boids 三规则已完成。** 模拟库可以独立于 Vulkan 构建和运行。默认应用仍显示带轨道相机、地面网格、世界边界和 ImGui 面板的程序化粒子预览，支持窗口缩放重建和最小化暂停绘制；真实 Boids 状态与渲染、参数面板和光标干预的连接安排在 Phase 3。

## 构建前置

- CMake 3.24 或更新版本。
- 支持 C++20 的编译器；下列预设使用 Visual Studio 2022、x64 目标和 MSVC 的 x64 宿主工具集。
- 构建应用时，需要完整的 LunarG Vulkan SDK，包括头文件、链接库和 `glslc`。显卡驱动附带的 Vulkan 运行时不足以编译项目。
- 首次配置需要联网下载固定版本的 GLFW、GLM 和 Dear ImGui。依赖版本及缓存说明见 [third_party/README.md](third_party/README.md)。

Phase 1 的历史验收记录（2026-10-22）：本机 SDK `1.4.363.0`（`D:\VulkanSDK\1.4.363.0`）的头文件、链接库、`glslc` 和 `VK_LAYER_KHRONOS_validation` 均可用。应用在 NVIDIA GeForce RTX 4060 Laptop GPU 上通过 Debug、Release 完整构建，当时两种配置的 CTest 均为 **3/3 通过**：`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外启用 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。模拟公共头文件独立编译和 `Agent` 布局检查也已通过。该记录不包含本次 Phase 2 新增的四项 CPU 测试。

Phase 2 验收记录（2026-10-22）：Debug / Release 全工程构建成功，两种配置的完整 CTest 均为 **7/7 通过**，包含 `sim_world_grid`、`sim_cpu`、`boids_rules`、`boids_simulation`、`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。独立 CPU 路径设置 `EMERGENCES_BUILD_APP=OFF`，在新目录 `build/sim-phase2` 完成 Debug / Release 构建，两种配置的 CPU 测试均为 **4/4 通过**；使用新目录是为了保留旧宿主工具集的 `build/sim-only` 缓存。

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

默认显示 2048 个粒子的渲染预览，面板提供以下操作：

- 数量 0–20000、箭头 / 圆点模式、粒子大小、朝向 / 速度 / 单色编码。
- 预览动画暂停与重置、相机重置、地面网格与世界边界开关。
- 左键拖动旋转、中键拖动平移、滚轮缩放；ImGui 捕获鼠标时，相机不响应相同输入。

关闭窗口或按 Escape 即可退出。命令行另提供两个独立的呈现测试：

```powershell
.\build-host-x64\Debug\emergences.exe --help
.\build-host-x64\Debug\emergences.exe --version
.\build-host-x64\Debug\emergences.exe --smoke-test
.\build-host-x64\Debug\emergences.exe --render-smoke-test
```

`--smoke-test` 保留原有渐变三角形测试：实际提交并呈现至少 12 帧，在第 4 和第 8 帧后调整窗口大小，确认至少 3 代交换链后退出。

`--render-smoke-test` 呈现至少 24 帧，依次使用 0 / 1 / 257 / 8192 / 96 / 2048 个粒子，覆盖两个帧槽的空数组、扩容与缩减，以及箭头 / 圆点、颜色、动画暂停、网格 / 边界、相机和 ImGui 绘制；同样执行两次缩放并要求至少 3 代交换链。两个测试在未完成检查就关闭窗口时失败，Debug 下要求校验层可用，并在 API 校验错误时返回失败。

Release 构建使用 `cmake --build --preset release`，程序位于 `build-host-x64/Release/emergences.exe`。`vs2022` 预设固定使用 `build-host-x64` 构建目录和 x64 宿主工具集；不使用预设时，可配置同等目标：

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

`vs2022-sim` 使用独立目录 `build/sim-only` 并设置 `EMERGENCES_BUILD_APP=OFF`。`emergences::sim` 包含 World、SpatialHash 和 CpuSimulation；`emergences::boids` 包含 Alignment / Cohesion / Separation，并传递链接模拟库。启用 `BUILD_TESTING` 时，模拟层和规则公共头文件分别独立编译，检查头文件自包含及 `Agent` 布局约束，同时注册以下 CPU 测试：

| CTest 名称 | 检查范围 |
| --- | --- |
| `sim_world_grid` | Wrap / Bounce 边界、最短周期位移、空间哈希与全量查询对照、半径与无重复邻居语义 |
| `sim_cpu` | 同步步进、规则加权、速度 / 加速度限幅、确定性初始化、重置 / 数量变化、干预和异常状态保持 |
| `boids_rules` | 三规则数学、独立半径、周期接缝、浮点半径边界、空邻域 / 重合 / 零半径及参数校验 |
| `boids_simulation` | 两种边界各 100 步全量邻域对照，以及 512 个体、240 步的确定性重放 |

这些测试也随完整应用构建注册。GPU 模拟尚未实现。

## 使用 CPU Boids 库

项目内的 CMake 目标链接 `emergences::boids`，即可使用以下最小组装方式：

```cpp
#include <memory>

#include "sim/cpu/CpuSimulation.h"
#include "scenarios/boids/Alignment.h"
#include "scenarios/boids/Cohesion.h"
#include "scenarios/boids/Separation.h"

int main() {
    using namespace emergences;
    sim::cpu::CpuSimulation simulation(512, sim::World{}, 1234U);
    simulation.rules().push_back(std::make_unique<scenarios::boids::Alignment>());
    simulation.rules().push_back(std::make_unique<scenarios::boids::Cohesion>());
    simulation.rules().push_back(std::make_unique<scenarios::boids::Separation>());
    simulation.setMotionLimits({1.0F, 8.0F, 20.0F});

    for (int step = 0; step < 240; ++step) {
        simulation.step(1.0F / 120.0F);
    }
    // simulation.agents() 提供只读状态，可由场景层转换为渲染记录。
}
```

`World` 默认是 `[-25,25]` 的周期世界（`BoundaryMode::Wrap`），也支持 `Bounce` 硬墙反弹和越过多次边界的位移。周期邻居距离、聚拢和分离均使用最短周期方向，因此两侧接缝附近的个体相互可见。世界范围必须有限且各轴跨度为可表示的正浮点数。

`SpatialHash` 每步按启用规则的最大半径重建稀疏均匀网格。通常每轴格宽至少为最大半径，查询相邻至多 27 格；若某轴世界跨度小于半径，则该轴仅使用一格。周期查询去重、按数组索引排除自身，最后以精确距离筛选。每条规则再按自己的半径筛选，包含恰好位于半径边界的邻居，半径为零时邻域为空。网格减少普通分布下的候选数量，但密集群体或大半径仍可退化为近似全量两两比较，不保证线性最坏复杂度。

三规则的默认 `weight / radius` 分别是 Alignment `1 / 5`、Cohesion `0.6 / 5`、Separation `1.5 / 2`。Alignment 返回邻居平均速度减自身速度；Cohesion 返回邻居最短位移的平均值；Separation 平均累加 `-displacement / max(distanceSquared, 1e-6)`，精确重合时跳过无方向的邻居。规则仅返回未加权加速度，后端乘权重一次，零权重禁用该规则。参数构造与修改均拒绝负数和非有限值。

每一步的所有规则读取同一份旧状态，后端合并并限制加速度，再按半隐式 Euler 更新速度、限制速度范围、更新位置并处理边界。默认最小 / 最大速度是 `1 / 8`，最大加速度是 `20`，可通过 `setMotionLimits` 调整；零速度需要提升到最小速度时使用由种子和 ID 决定的方向。`step(dt)` 接受有限非负秒数，`dt=0` 不推进；建议以固定步长运行，并由应用累积帧时间，避免帧率变化改变模拟轨迹。

初始化由种子和索引确定。`reset()` 按当前数量、世界和速度限制重新生成初始状态；`setCount()` 缩减时保留前缀，增加时保留现有个体并确定性地生成追加个体。`setAgents()` 可导入状态且保留提供的 ID；ID 是元数据，不是邻域数组索引，导入与追加组合使用时不自动保证唯一。`setWorld()`、`setMotionLimits()` 和状态导入会重新规范速度与边界。

干预在积分后按插入顺序执行，可以修改个体数量、状态和世界。全部干预结束后，后端校验最终状态、重施速度限制与边界，再提交新状态；失败的步进保留外部可见的旧个体和世界，但无法撤销回调对这些记录以外的副作用。规则与干预集合只能在步进之间修改，不能包含空指针。当前提供干预接口，光标吸引 / 排斥的具体实现与交互在 Phase 3 加入。

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
| `camera_math` | 相机默认取景、Vulkan 深度 / Y 轴约定、旋转 / 平移 / 缩放、重置和极端输入 |
| `vulkan_triangle` | `--smoke-test` 的三角形呈现和交换链缩放 |
| `vulkan_particles` | `--render-smoke-test` 的粒子、辅助线、面板、相机和缓冲扩容路径 |

两个 GPU 测试会短暂创建窗口并自动缩放。CTest 会检查输出，在任何 `[Vulkan validation]` 消息（包括资源销毁时的消息）出现时失败；Loader 的一般诊断仍会输出。GPU 测试默认关闭，`camera_math` 在构建应用且启用 `BUILD_TESTING` 时注册。完整应用套件包含四项 CPU 模拟测试与 `camera_math`，启用 GPU 测试后再加入两项呈现测试；单独模拟构建运行上述四项 CPU 测试。

需要额外复查同步时，可在当前 PowerShell 会话设置 `$env:VK_LAYER_VALIDATE_SYNC = '1'` 后运行 `ctest --preset debug`，完成后以 `Remove-Item Env:VK_LAYER_VALIDATE_SYNC` 清除该会话设置。

## 代码结构与接口约定

| 位置 | 当前职责 |
| --- | --- |
| `src/core/` | Window / Context；带逐图像深度附件的 Swapchain；共享命令记录、双帧同步与呈现的 FrameRenderer；Pipeline / Buffer / Descriptor；Camera、TriangleRenderer、ParticleRenderer、LineRenderer、ImGuiLayer |
| `src/sim/` | `Agent`、`World`、`Rule`、`Intervention`、`Simulation` 和后端枚举；`cpu/` 下为 SpatialHash 与 CpuSimulation |
| `src/scenarios/boids/` | 纯 CPU 的 Boids 三规则与测试；应用入口、程序化粒子预览、面板和两类呈现测试，Phase 3 对接真实模拟 |
| `shaders/` | `particle.*` 粒子、`grid.*` 辅助线及 `smoke/triangle.*` 三角形着色器 |
| `cmake/` | 依赖获取与着色器编译辅助配置 |
| `third_party/` | 第三方依赖说明；实际源码由 FetchContent 放入构建目录 |

`Agent` 使用 16 字节对齐、48 字节固定布局，包含 position / size、velocity / reserved、color / id 三组数据；字段偏移通过 `static_assert` 固定，以便后续与 GLSL `std430` 记录对应。应使用 GLM 默认的 packed `vec3`，不能直接启用会改变布局的 aligned 向量类型。

`Neighbor` 是 CPU 规则调用期间借用的邻居记录，使用 `double distanceSquared` 保留半径边界比较的精度；它不属于 `Agent` 的 GPU 记录，不改变上述 48 字节布局。

渲染层使用独立的 `core::Particle` 记录和 `SceneView` 相机常量。粒子数据经各帧槽的 storage buffer 上传，顶点着色器通过 `gl_InstanceIndex` 读取，生成箭头或圆点 billboard；辅助线使用静态顶点缓冲。`FrameRenderer` 统一负责三角形与预览的提交，交换链每幅图像各有深度附件，ImGui 的 Vulkan 资源在重建时重新配置。

Vulkan 实现参考：[描述符布局与缓冲](https://docs.vulkan.org/tutorial/latest/05_Uniform_buffers/00_Descriptor_set_layout_and_buffer.html)、[深度缓冲](https://docs.vulkan.org/tutorial/latest/07_Depth_buffering.html)。

模拟层不依赖 Vulkan。`Simulation::agents()` 返回借用的只读视图：CPU 后端提供个体数组，视图在下一次修改时失效；未来 GPU 后端返回空视图，不能隐式触发 GPU 回读。GPU 资源与同步桥接留待 Phase 4。规则和干预由模拟对象通过 `unique_ptr` 持有，仅在步进之间修改；干预仅适用于 CPU，未来 GPU 后端必须拒绝非空干预列表。

## 后续实现顺序

1. **Phase 3**：将 Phase 2 的真实模拟接入现有渲染器与面板，加入规则权重 / 半径、速度限制和光标吸引 / 排斥干预。
2. **Phase 4**：在 CPU 场景验证后实现 GPU compute、资源与同步桥接及后端切换。
