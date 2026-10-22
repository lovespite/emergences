# emergences · 涌现模拟

基于 C++20 和 Vulkan 的涌现模拟项目。项目按通用渲染层、通用模拟层和具体场景三层组织，首个场景计划实现 Boids 鸟群。完整方案见 [PLAN.md](PLAN.md)。

当前已完成 **Phase 1：Vulkan 渲染框架**。默认程序显示带轨道相机、地面网格、世界边界和 ImGui 面板的粒子预览，支持窗口缩放重建和最小化暂停绘制。粒子按确定性公式生成运动，用于验证渲染；Boids 邻域查询、行为规则和 CPU 模拟将在 Phase 2 实现。

## 构建前置

- CMake 3.24 或更新版本。
- 支持 C++20 的编译器；下列预设使用 Visual Studio 2022、x64 目标和 MSVC 的 x64 宿主工具集。
- 构建应用时，需要完整的 LunarG Vulkan SDK，包括头文件、链接库和 `glslc`。显卡驱动附带的 Vulkan 运行时不足以编译项目。
- 首次配置需要联网下载固定版本的 GLFW、GLM 和 Dear ImGui。依赖版本及缓存说明见 [third_party/README.md](third_party/README.md)。

2026-10-22 已验证本机 SDK `1.4.363.0`（`D:\VulkanSDK\1.4.363.0`）：头文件、链接库、`glslc` 和 `VK_LAYER_KHRONOS_validation` 均可用。应用在 NVIDIA GeForce RTX 4060 Laptop GPU 上通过 Debug、Release 完整构建，两种配置的 CTest 均为 **3/3 通过**：`camera_math`、`vulkan_triangle`、`vulkan_particles`。Debug 额外启用 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。模拟公共头文件独立编译和 `Agent` 布局检查也已通过。

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

## 不依赖 Vulkan 的模拟接口构建

尚未安装 SDK 时，可以单独配置模拟接口。该路径需要 GLM，首次配置仍需下载依赖，但不构建 Vulkan 应用。

```powershell
cmake --preset vs2022-sim
cmake --build --preset sim-debug
```

`vs2022-sim` 使用独立目录 `build/sim-only` 并设置 `EMERGENCES_BUILD_APP=OFF`。模拟层目前是仅含头文件的接口目标；启用 `BUILD_TESTING` 时，`emergences_sim_header_check` 对各公共头文件分别编译，检查头文件自包含及 `Agent` 布局约束。CPU 和 GPU 模拟实现尚未加入，此处仅执行编译检查。

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

两个 GPU 测试会短暂创建窗口并自动缩放。CTest 会检查输出，在任何 `[Vulkan validation]` 消息（包括资源销毁时的消息）出现时失败；Loader 的一般诊断仍会输出。GPU 测试默认关闭，`camera_math` 在构建应用且启用 `BUILD_TESTING` 时注册。模拟接口单独构建仍只执行头文件编译检查。

需要额外复查同步时，可在当前 PowerShell 会话设置 `$env:VK_LAYER_VALIDATE_SYNC = '1'` 后运行 `ctest --preset debug`，完成后以 `Remove-Item Env:VK_LAYER_VALIDATE_SYNC` 清除该会话设置。

## 代码结构与接口约定

| 位置 | 当前职责 |
| --- | --- |
| `src/core/` | Window / Context；带逐图像深度附件的 Swapchain；共享命令记录、双帧同步与呈现的 FrameRenderer；Pipeline / Buffer / Descriptor；Camera、TriangleRenderer、ParticleRenderer、LineRenderer、ImGuiLayer |
| `src/sim/` | `Agent`、`World`、`Rule`、`Intervention`、`Simulation` 和后端枚举 |
| `src/scenarios/boids/` | 应用入口、程序化粒子预览、面板和两类呈现测试；后续接入 Boids 模拟 |
| `shaders/` | `particle.*` 粒子、`grid.*` 辅助线及 `smoke/triangle.*` 三角形着色器 |
| `cmake/` | 依赖获取与着色器编译辅助配置 |
| `third_party/` | 第三方依赖说明；实际源码由 FetchContent 放入构建目录 |

`Agent` 使用 16 字节对齐、48 字节固定布局，包含 position / size、velocity / reserved、color / id 三组数据；字段偏移通过 `static_assert` 固定，以便后续与 GLSL `std430` 记录对应。应使用 GLM 默认的 packed `vec3`，不能直接启用会改变布局的 aligned 向量类型。

渲染层使用独立的 `core::Particle` 记录和 `SceneView` 相机常量。粒子数据经各帧槽的 storage buffer 上传，顶点着色器通过 `gl_InstanceIndex` 读取，生成箭头或圆点 billboard；辅助线使用静态顶点缓冲。`FrameRenderer` 统一负责三角形与预览的提交，交换链每幅图像各有深度附件，ImGui 的 Vulkan 资源在重建时重新配置。

Vulkan 实现参考：[描述符布局与缓冲](https://docs.vulkan.org/tutorial/latest/05_Uniform_buffers/00_Descriptor_set_layout_and_buffer.html)、[深度缓冲](https://docs.vulkan.org/tutorial/latest/07_Depth_buffering.html)。

模拟层不依赖 Vulkan。`Simulation::agents()` 返回借用的只读视图：CPU 后端提供个体数组，视图在下一次修改时失效；未来 GPU 后端返回空视图，不能隐式触发 GPU 回读。GPU 资源与同步桥接留待 Phase 4。规则和干预由模拟对象通过 `unique_ptr` 持有，仅在步进之间修改；干预仅适用于 CPU，未来 GPU 后端必须拒绝非空干预列表。

## 后续实现顺序

1. **Phase 2**：实现 CPU 模拟、空间哈希邻域、积分与周期边界，以及 Alignment / Cohesion / Separation 三条 Boids 规则。
2. **Phase 3**：将真实模拟接入现有渲染器与面板，加入规则权重 / 半径、速度限制和光标吸引 / 排斥干预。
3. **Phase 4**：在 CPU 场景验证后实现 GPU compute、资源与同步桥接及后端切换。
