# 涌现模拟软件（emergences）— 实现方案

## 实施进度（2026-10-23）

- 实际项目目录为 `D:\Projects\emergences`。
- 已建立 C++20 / CMake 构建、VS2022 预设、固定提交与校验值的 FetchContent 依赖、GLSL 构建期编译入口。
- 已建立 `core`、`sim`、`scenarios/boids` 三层目标；Window / Context 实现 GLFW 与 Vulkan 初始化，模拟层提供 CPU 后端，场景层提供独立 Boids 规则库及应用入口。
- 已验证 LunarG Vulkan SDK 1.4.363.0、glslc、Khronos 校验层与 RTX 4060；模拟公共头文件独立编译及 Agent 布局检查通过。
- **Phase 1 已完成**：Swapchain / Pipeline / Buffer / Descriptor、Camera、ParticleRenderer / LineRenderer 和 ImGuiLayer 已接入。FrameRenderer 统一管理命令缓冲、双帧提交、每交换链图像的呈现信号量、缩放重建与最小化暂停；每幅交换链图像拥有独立深度附件。
- 默认程序运行 `[-40,40]` 世界中的 2048 个真实 CPU Boids，使用固定 `1/120` 秒步长、单帧最多 8 步；交互推进另有 8 ms 工作预算，在完整步之间检查，超额时放慢模拟时间并显示提示。支持 0–20000 数量、暂停 / 单步 / 重置、三规则权重 / 半径、速度 / 加速度限制、Wrap / Bounce 边界和四种颜色。`--render-preview` 保留 Phase 1 程序化粒子预览。
- **Phase 1 历史验收**：Debug、Release 完整构建通过；当时两种配置的 `camera_math`、`vulkan_triangle`、`vulkan_particles` 均为 3/3 通过。Debug 另以 `VK_LAYER_VALIDATE_SYNC=1` 检查，无校验诊断。该历史记录不包含 Phase 2 新增测试。
- 保留 12 帧三角形冒烟测试；新增至少 24 帧粒子冒烟测试，覆盖 0 / 1 / 257 / 8192 / 96 / 2048 粒子、缓冲扩容与缩减、模式 / 相机 / 面板和两次缩放，要求至少 3 代交换链。SDK 截图层已检查箭头、圆点、缩放画面，产物在 `build-host-x64/verification-phase1/`；未执行面板逐项点击的 UI 自动化。
- **Phase 2 已完成**：World 的周期 / 反弹边界与最短位移、SpatialHash 邻域、CpuSimulation 同步积分、速度 / 加速度限制、确定性重置 / 数量调整和干预提交；Alignment / Cohesion / Separation 组成不依赖 Vulkan 的 `emergences::boids` 库。
- 已注册 `sim_world_grid`、`sim_cpu`、`boids_rules`、`boids_simulation` 四项 CPU 测试，提供 `sim-debug` / `sim-release` 构建与测试预设。**Phase 2 验收**：Debug / Release 全工程构建成功，完整 CTest 各 7/7 通过（四项 CPU 测试、相机和两项呈现测试），Debug 开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。独立 CPU 路径设置 `EMERGENCES_BUILD_APP=OFF`，在 `build/sim-phase2` 新目录构建 Debug / Release，两种配置 CPU 测试各 4/4 通过；旧 `build/sim-only` 工具集缓存保留未动。
- **Phase 3 已完成**：BoidsScene 管理固定步长与独立显示记录；LiveBoids 连接模拟、粒子渲染和 Behavior / View 面板；光标通过屏幕射线投影到可调高度的 XZ 平面，提供球形邻域的吸引 / 排斥及范围圆环，UI / 相机操作、鼠标离开窗口和窗口失焦时关闭施力。GPU 后端选项灰显。
- **Phase 3 验收**：Debug / Release 全工程构建成功，完整 CTest 各 **10/10 通过**；Debug 开启 `VK_LAYER_VALIDATE_SYNC=1` 后无校验诊断。新增 `boids_scene`、`boids_cursor`、`vulkan_boids`，相机测试增加射线 / 平面拾取。在 `build/sim-phase3` 独立配置 `EMERGENCES_BUILD_APP=OFF`，Debug / Release 构建及六项 CPU 测试各 **6/6 通过**。真实场景冒烟通过 48 帧、两次缩放及 3 代交换链，并验证吸引 / 排斥轨迹不同于未施力对照。SDK 截图已检查真实鸟群聚集、面板、密度色、圆点和干预范围，产物在 `build-host-x64/verification-phase3/`。未执行逐项 GUI 点击自动化或正式性能基准。
- **下一阶段为 Phase 4**：CPU 场景验收后实现 GPU compute、资源与同步桥接及后端切换。
- **Phase 3 长时性能修正**：复现了密集鸟群邻居增多与八步补算叠加造成的约 1 FPS。空间查询缓存最短位移、最大半径规则复用邻居列表，交互推进加入工作预算。历史测量使用当时的默认规则参数（`weight / radius`：Alignment `1 / 5`、Cohesion `0.6 / 5`、Separation `1.5 / 2`），不代表当前默认参数的性能：相同 20 秒状态的 Release 单步约 80.6→15.2 ms；八步约 624 ms 的交互推进改为预算下一步约 14 ms（过载时模拟时钟减速）。该组参数下 2048 个体连续 2400 步各检查点与原实现逐字节一致。回归增加密集呈现、预算耗尽、缓存与非缓存规则对照及极端周期边界检查，记录见 README 与 `build/perf-diagnosis/`。
- **CPU 三规则融合与可选多线程**：BoidsScene 自动安装场景层的 `BoidsRuleKernel`，一次邻居遍历累加三规则；模拟层通过通用 `RuleKernel` 接口调度支持的并行计算，每步刷新参数快照。通用规则与不支持的规则集合继续串行。空间网格保持原实现，新增 `sim_cpu_parallel`、`boids_kernel`，当前 CPU 测试注册数为八项。OpenMP 可选，GPU compute 仍属于 Phase 4。

构建与当前实现边界见 [README.md](README.md)。

## Context（背景）

从零构建一个「涌现模拟」软件，渲染基于 Vulkan。目标是三层的通用架构，而非只做鸟群：

1. **通用可视化框架**（Vulkan 渲染，与模拟无关）
2. **通用模拟框架**（设定个体行为 / 数量 / 规则）
3. **场景 / 案例**（首个案例：鸟群 Boids）

核心约束（已与用户确认）：

- **C++20 + CMake**，Visual Studio 2022 已就绪，Vulkan 用 LunarG SDK（用户自行安装）。
- **双后端模拟**：有干预需求走 **CPU**，自行演化走 **GPU compute**，按需切换。同一套规则要在两个后端都可用。
- 参数调节用 **Dear ImGui**（实时调行为/数量/规则权重）。
- 初始目录 `D:\Projects\emergences` 仅有计划文件，从零新建。

关键环境事实：LunarG SDK 已安装于 `D:\VulkanSDK\1.4.363.0`，头文件、glslc、链接库和校验层均已验证。构建使用 `find_package(Vulkan)` + `find_program(glslc)`，不硬编码本机 SDK 路径；安装前打开的终端需重新加载 `VULKAN_SDK`。

---

## 架构总览

三层 + 一个后端切换轴：

```
┌─────────────────────────────────────────────────────────┐
│  场景层  scenarios/boids                                 │
│    BoidsApp：组装 core + sim，主入口，ImGui 面板          │
│    - 规则：Alignment / Cohesion / Separation             │
│    - 干预：CursorRepel / CursorAttract（可选，可增删）      │
├─────────────────────────────────────────────────────────┤
│  模拟层  sim/（与 Vulkan 完全解耦）                        │
│    Simulation（后端无关接口）                             │
│      ├─ cpu::CpuSimulation   —— 规则 C++，可挂干预        │
│      └─ gpu::GpuSimulation   —— 规则 GLSL compute，自行演化│
│    Agent / World / Rule / Intervention 抽象              │
├─────────────────────────────────────────────────────────┤
│  渲染层  core/（通用 Vulkan 可视化框架）                   │
│    Window · Context · Swapchain · Pipeline · Buffer      │
│    FrameRenderer · Descriptor · Camera · ParticleRenderer │
│    LineRenderer · ImGuiLayer                              │
└─────────────────────────────────────────────────────────┘
```

上图中的 CPU 后端、Boids 三规则、光标干预和场景连接已实现，GPU 后端留待 Phase 4。**关键设计**：模拟层只产出「个体状态」（position / velocity / color）。场景生成独立的显示颜色副本，再转换为 `core::Particle` 记录供渲染层消费；着色不会改变模拟轨迹。CPU 路径已接通，GPU 路径按以下接口方向扩展：

- CPU 后端：`agents()` 返回 CPU 数组 → 渲染层每帧上传到 GPU 实例缓冲。
- GPU 后端：compute 直接写个体 SSBO → 渲染层把同一块 SSBO 当实例缓冲绑定（**零拷贝**）。

「同一套规则、两个后端」的实现策略（boids 规则各只有 3 行数学）：**C++ 与 GLSL 各写一份、保持同步**，规则数学在两侧注释互指。规则数量少时这是最省事、最透明的做法；若未来规则激增再引入「单源头文件 + 宏」方案。

---

## 目录结构

以下包含已落地的渲染层、CPU 模拟、Boids 场景与光标干预，以及标注阶段的后续 GPU 文件规划。

```
emergences/
├── CMakeLists.txt
├── README.md
├── shaders/
│   ├── particle.vert          # 实例化朝向三角（箭头）顶点着色器
│   ├── particle.frag
│   ├── grid.vert / grid.frag  # 世界边界 / 网格辅助线
│   ├── smoke/triangle.*       # 保留的三角形冒烟测试
│   └── compute/
│       └── boids.comp         # GPU 后端（Phase 4）
├── src/
│   ├── core/                  # 通用 Vulkan 可视化框架
│   │   ├── Window.h/.cpp              # GLFW 封装
│   │   ├── Context.h/.cpp             # instance/device/queues
│   │   ├── Swapchain.h/.cpp           # 交换链 + 每图像深度附件 + framebuffer
│   │   ├── Pipeline.h/.cpp            # 可配置图形管线；计算管线留待 Phase 4
│   │   ├── TriangleRenderer.h/.cpp    # 原有三角形，复用 FrameRenderer
│   │   ├── Buffer.h/.cpp              # 缓冲/内存所有权、映射写入与 flush
│   │   ├── FrameRenderer.h/.cpp       # 共享命令记录、双帧同步、提交/呈现与重建
│   │   ├── Descriptor.h/.cpp          # 布局 + 池 + 集
│   │   ├── Camera.h/.cpp              # 轨道相机、屏幕射线与平面拾取
│   │   ├── ParticleRenderer.h/.cpp    # 实例化朝向三角 + 点模式
│   │   ├── LineRenderer.h/.cpp        # 边界/网格
│   │   └── ImGuiLayer.h/.cpp          # ImGui + Vulkan/GLFW backend
│   ├── sim/                   # 通用模拟框架（不依赖 Vulkan）
│   │   ├── Agent.h                    # 个体状态
│   │   ├── World.h/.cpp               # 周期 / 反弹边界、最短周期位移与校验
│   │   ├── Rule.h                     # 规则接口
│   │   ├── Intervention.h             # 干预接口
│   │   ├── Backend.h                  # enum CPU / GPU
│   │   ├── Simulation.h               # 后端无关抽象
│   │   ├── ComputeDevice.h            # GPU 薄设备接口，core 实现（Phase 4）
│   │   ├── cpu/SpatialHash.h/.cpp     # 稀疏均匀网格与邻域查询
│   │   ├── cpu/RuleKernel.h          # 可选规则融合与线程安全计算接口
│   │   ├── cpu/CpuSimulation.h/.cpp   # CPU 后端、同步积分与干预
│   │   └── gpu/GpuSimulation.h/.cpp   # GPU 后端（Phase 4）
│   └── scenarios/boids/
│       ├── BoidsApp.h/.cpp            # 应用接口与三角形测试
│       ├── LiveBoids.cpp             # 真实模拟、渲染、面板、光标范围和冒烟测试
│       ├── BoidsScene.h/.cpp          # 纯 CPU 的固定步长、控制状态与显示着色
│       ├── RenderPreview.cpp         # 保留的程序化预览与粒子冒烟测试
│       ├── Alignment.h/.cpp
│       ├── Cohesion.h/.cpp
│       ├── Separation.h/.cpp
│       ├── BoidsRuleKernel.h/.cpp    # 三规则融合、参数快照与原顺序加权
│       ├── CursorIntervention.h/.cpp  # 光标力参数、周期距离与平滑衰减
│       ├── CursorRepel.h/.cpp         # 光标排斥干预
│       ├── CursorAttract.h/.cpp       # 光标吸引干预
│       └── main.cpp
└── third_party/README.md      # 依赖说明；FetchContent 源码位于构建目录
```

---

## 核心技术决策

| 主题 | 决策 |
|---|---|
| 依赖获取 | GLFW / GLM / ImGui 走 `FetchContent`（轻量、无需 vcpkg）；Vulkan 走 SDK 的 `find_package(Vulkan)` |
| 着色器编译 | CMake 自定义命令 `glslc` → `.spv`，构建期生成；校验层 Debug 开启 |
| 个体渲染 | **实例化朝向三角 / 圆点 billboard**：各帧槽使用独立的 48 字节粒子记录 SSBO；顶点着色器通过 `gl_InstanceIndex` 读取记录，按 `gl_VertexIndex` 生成三角或六顶点圆点面片，朝向按投影速度计算。单次 draw call 画全部个体 |
| 颜色编码 | 真实场景提供朝向 / 速度 / 局部密度 / 单色；只修改显示副本。密度统计半径 5 内邻居，固定 0–32 色阶并饱和，支持周期接缝 |
| 相机与光标 | 左键拖旋转、滚轮缩放、中键平移；屏幕射线投影到可调高度的 XZ 平面，球形范围由三个圆环显示。UI 捕获、相机输入或失焦时暂停光标施力 |
| 世界边界 | BoidsScene 显式使用与渲染一致的 `[-40,40]`；通用 World 默认 `[-25,25]`。支持**周期包裹（torus）**、硬墙反弹及多次越界处理 |
| 邻域查询（CPU） | 每步按启用规则最大半径重建稀疏均匀网格；通常格宽不小于该半径，跨度较小的轴使用一格。查询相邻 27 格、周期去重、精确距离过滤；密集或大半径查询可退化为全量两两比较 |
| 积分（CPU） | 所有规则读取旧状态，权重求和 → clamp 加速度 → `vel += acc*dt` → clamp 速度上下限 → `pos += vel*dt` → 边界处理。BoidsScene 固定 `1/120` 秒，每帧最多 8 步；交互应用加入 8 ms 工作预算，丢弃超额整步积压 |
| 规则融合与并行（CPU） | 场景层提供 BoidsRuleKernel，模拟层只依赖 RuleKernel 抽象。每步主线程准备参数快照，支持的规则集合按个体并行；保留各规则累计精度、邻居顺序和原加权顺序。网格重建、干预和最终校验保持串行 |
| 后端切换 | `Backend` 枚举 + ImGui 下拉已接入，当前 CPU 可用、GPU 灰显；Phase 4 实现 GPU 自行演化、大 N |

---

## 关键接口（模拟框架核心）

```cpp
// sim/Agent.h —— 个体状态，由场景层转换为渲染记录
struct alignas(16) Agent {
    glm::vec3 position;
    float size;
    glm::vec3 velocity;
    float reserved;
    glm::vec3 color;      // 显示颜色，由规则/密度/朝向映射
    uint32_t  id;
};
// 48 字节，字段偏移为 0/12/16/28/32/44，与 std430 记录对应。

// sim/Rule.h —— 一条行为规则：读自己 + 邻居 → 输出 steering 加速度
struct Neighbor {
    const Agent* agent;
    double distanceSquared;
    std::optional<glm::vec3> displacement{}; // 查询缓存的 self→邻居最短位移
};
// double 保留半径边界比较精度；Neighbor 不属于 Agent 的 GPU ABI。
class Rule {
public:
    virtual ~Rule() = default;
    virtual glm::vec3 steer(const Agent& self, std::span<const Neighbor> nbrs,
                            const World& w) const = 0;
    virtual std::string_view name() const noexcept = 0;
    virtual float weight() const noexcept = 0;       // ImGui 实时可调
    virtual float radius() const noexcept = 0;
    virtual void setWeight(float) = 0;
    virtual void setRadius(float) = 0;
};

// sim/Intervention.h —— 可选干预：在规则之后修改个体/世界（仅 CPU 后端）
class Intervention {
public:
    virtual ~Intervention() = default;
    virtual void apply(float dt, std::vector<Agent>& agents, World& w) = 0;
    virtual std::string_view name() const noexcept = 0;
};

// sim/Simulation.h —— 后端无关抽象（由场景层与渲染记录对接）
class Simulation {
public:
    virtual ~Simulation() = default;
    virtual void step(float dt) = 0;
    virtual void reset() = 0;
    virtual size_t count() const noexcept = 0;
    virtual void setCount(size_t) = 0;
    virtual Backend backend() const noexcept = 0;
    virtual std::span<const Agent> agents() const noexcept = 0;
    // CPU：借用视图，下次修改即失效；GPU：空视图，不隐式回读。
    // 规则/干预归模拟对象所有，仅在步进之间修改集合。
    virtual std::vector<std::unique_ptr<Rule>>& rules() noexcept = 0;
    virtual std::vector<std::unique_ptr<Intervention>>& interventions() noexcept = 0;
};
```

Boids 三规则已实现为三个 `Rule` 子类（Alignment/Cohesion/Separation），各自带有限非负的 weight/radius，默认值依次为 `0.45/5`、`0.11/6`、`2/2`。`steer` 返回未加权加速度，CPU 后端统一乘权重，零权重禁用；零半径的邻域为空。Alignment 使用邻居平均速度减自身速度，Cohesion 使用最短周期位移的平均值，Separation 使用 `-displacement / max(distanceSquared, 1e-6)` 的平均值并忽略精确重合。独立规则也执行各自半径过滤。

`CpuSimulation` 提供 `world/setWorld`、`motionLimits/setMotionLimits` 和 `setAgents`。默认最小 / 最大速度为 `1/8`、最大加速度为 `20`；导入状态、世界和速度限制变化后会重新规范速度与边界。`reset` 按当前数量、世界、速度限制与种子重建初始状态，`setCount` 保留现有前缀并确定性地产生新增个体。状态中的 ID 是元数据，不作为邻域索引；导入 ID 与新增索引组合时由调用者管理唯一性。最小组装示例见 [README.md](README.md)。

`setRuleKernel` 安装可选的 `cpu::RuleKernel`；`prepare` 每步在规则校验后、主线程上执行，支持时缓存完整参数供只读的 `acceleration` 并行使用。`BoidsRuleKernel` 位于场景层，支持 Alignment / Cohesion / Separation 各一个的任意排列，保留原规则加权顺序；自定义、缺失、重复规则或未安装 kernel 时回退通用串行路径。这样 `sim` 不依赖 Boids 类型，且本次不更改空间网格。

`setWorkerCount(0)` 默认自动选择：个体少于 512 时串行，否则取运行时可见逻辑处理器数的一半，至少 1 个、至多 16 个线程，并受 OpenMP 运行时上限和个体数量限制。`setWorkerCount(1)` 固定串行，大于 1 为请求线程数，受运行时、逻辑处理器数和个体数量限制。`workerCount()` 返回配置值；只对支持的 kernel 并行。CMake 的 `EMERGENCES_ENABLE_OPENMP` 默认 `ON`，检测到支持即启用，缺少支持则串行；设为 `OFF` 可验证兼容路径，仍保留三规则融合。kernel、线程数与规则的修改均在步进之间进行。

干预在规则积分后按插入顺序执行；最终状态与世界通过校验、速度和边界规范后才提交。失败的步进保留可见的旧个体 / 世界，回调在这些记录之外的副作用无法回滚。规则和干预集合只允许在步进之间修改。`CursorRepel` / `CursorAttract` 使用世界最短位移计算球形邻域，按 reverse smoothstep 随距离平滑衰减，以强度乘 `dt` 改变速度；后端随后重施速度限制。ImGui 提供开关、模式、半径、强度和交互平面高度，屏幕拾取失败或输入被占用时停用干预。

`BoidsScene` 组合 CPU 模拟与三规则，自动安装 `BoidsRuleKernel` 并采用默认自动线程策略，提供 `advance`、`setPaused`、`singleStep`、`reset`、模拟时钟和 `refreshColors`。暂停不积累时间，暂停切换和重置清除不足一步的时间；重置保留数量、参数和暂停状态。显示颜色复制自模拟状态，密度通过独立空间网格查询，不改写模拟记录。

---

## 分阶段实施

### Phase 1 — Vulkan 渲染框架（core）+ 冒烟测试（已完成）

1. `CMakeLists.txt`：C++20、`FetchContent`(GLFW/GLM/ImGui)、`find_package(Vulkan)`、`glslc` 着色器编译函数、Debug 配置开启校验层。
2. `Window` / `Context` / `Swapchain` / `Pipeline` / `Buffer` / `FrameRenderer` / `Descriptor`。命令记录与提交统一由 `FrameRenderer` 管理，无独立 `CommandBuffer` 包装类。
3. **三角形冒烟测试**（已完成）→ 打通 instance→present 全链路，自动呈现 12 帧并验证两次窗口缩放。
4. `ParticleRenderer`（从 CPU 数组画 N 个朝向三角 / 圆点）、`LineRenderer`（边界/网格）、`Camera`、`ImGuiLayer`；程序化预览与粒子 / 相机测试已通过。

### Phase 2 — 模拟框架 + CPU 后端（已完成）

5. `Agent/World/Rule/Intervention/Simulation/Backend` 抽象已建立；World 实现周期最短位移、Wrap / Bounce 边界与校验，保持 Agent 48 字节 ABI。
6. `cpu::SpatialHash` 与 `cpu::CpuSimulation`：独立半径邻域、同步积分、加速度 / 速度限制、确定性初始化 / 重置 / 数量调整、干预顺序和异常提交保证。
7. Boids 三规则 `Alignment/Cohesion/Separation` 与纯 CPU 库；四项无 Vulkan CTest 覆盖规则、后端、全量邻域对照及多步重放。

### Phase 3 — Boids 场景 + 交互（已完成）

8. `BoidsScene` 管理 120 Hz 固定步长与独立显示数据；`LiveBoids` 实现 sim→renderer 对接、每帧上传和主循环。原预览由 `--render-preview` 保留。
9. ImGui 面板包含个体数量、三规则 weight/radius、速度 / 加速度限制、暂停 / 单步 / 重置、边界模式、后端选择（GPU 灰显）以及四种颜色。密度使用固定半径和色阶，避免显示变化影响模拟。
10. `CursorRepel/Attract` 与屏幕射线拾取、可调 XZ 平面、范围圆环、输入占用保护已接入；新增两项 CPU 场景 / 干预测试、扩展相机测试和 48 帧真实 Boids 呈现测试。Debug / Release 构建、完整测试与独立 CPU 测试通过，结果见上方实施进度。

### Phase 4 — GPU 后端（compute）

11. 计算管线基建：compute shader、SSBO、dispatch、`ComputeDevice` 接口（core 实现）。
12. `boids.comp`：三规则 GLSL + **均匀网格**（cell→agent 列表排序 + 前缀和，查询 27 邻域）。
13. `gpu::GpuSimulation`：compute 写个体 SSBO，渲染层零拷贝绑定；ImGui 后端切换可用（CPU=带干预 / GPU=大 N 自行演化）。

**执行顺序承诺**：先完成 Phase 1–3 得到一个可运行、可干预的鸟群 demo，验证后再进 Phase 4。

---

## 验证方式

1. **CPU 独立构建与自动检查**：`cmake --preset vs2022-sim`，随后 `cmake --build --preset sim-debug`、`ctest --preset sim-debug`，Release 对应 `sim-release`。不需要 Vulkan SDK；八项测试为 `sim_world_grid`、`sim_cpu`、`sim_cpu_parallel`、`boids_rules`、`boids_kernel`、`boids_simulation`、`boids_scene`、`boids_cursor`。除原有全量邻域对照、确定性重放、场景和干预检查外，新增融合与独立规则的逐分量位对照、参数快照 / 重排 / 回退，以及串行 / 并行轨迹、工作线程异常和提交顺序。可在独立构建目录设置 `EMERGENCES_ENABLE_OPENMP=OFF` 检查串行兼容路径，命令见 README。完整应用配置 `cmake --preset vs2022 -DEMERGENCES_ENABLE_GPU_TESTS=ON` 后，Debug 使用 `ctest --preset debug`，Release 使用 `ctest --test-dir build-host-x64 -C Release --output-on-failure`，另包含 `camera_math`、`vulkan_triangle`、`vulkan_particles` 和 `vulkan_boids`，合计十二项；完整构建需要 Vulkan SDK。
2. **渲染回归与真实场景冒烟**：`--render-preview` 打开 Phase 1 程序化预览；`--render-smoke-test` 保留粒子数量 / 缓冲变化、相机、UI 绘制和缩放回归。`--boids-smoke-test` 呈现至少 48 帧真实模拟，检查数量、暂停 / 单步 / 重置、规则、限幅、边界、颜色、光标开关 / 输入抑制和两次缩放。自动测试通过程序设置参数，不等同于鼠标逐项点击面板。
3. **校验层**：Debug 运行无 `VK_LAYER_KHRONOS_validation` 报错；可设置 `VK_LAYER_VALIDATE_SYNC=1` 加验同步。
4. **Phase 3 交互复查项目**：实现已接通，以下交互行为可按 README 说明复查；本轮尚未声明逐项 GUI 点击验收完成：

   - 窗口出现，N 只三角鸟随时间自发「聚拢 + 对齐 + 分离」，形成可见鸟群/涡旋。
   - ImGui 里拖动「分离/对齐/聚拢」权重、半径，行为实时变化；调数量 N 平滑增减。
   - 暂停后状态保持，单步只推进 `1/120` 秒；重置保留参数并清空模拟时钟。
   - 打开光标排斥 / 吸引，悬停时作用于选定高度附近的球形邻域；在 UI 上、相机输入期间或失焦时停止施力。
   - 在 View 中切换方向 / 速度 / 密度 / 单色，以及箭头 / 圆点、网格和边界。

5. **Phase 4 后续目标**：ImGui 切到 GPU 后端，验证 10 万级个体的性能与 CPU 版行为一致性（同规则、同参数）；当前尚未实现或测得此规模性能。

---

## 风险与后续

- **Vulkan 应用需要完整 SDK**：本机已安装并验证；独立 CPU 模拟与测试仅依赖 GLM。
- 空间哈希在局部密度受控时减少候选查询，但不保证最坏线性复杂度。10 万级模拟属于后续 GPU 目标，本阶段未承诺 CPU 的该规模性能。
- GPU 后端的「均匀网格」compute 是标准但中等复杂度的部分，故单列为 Phase 4，不与 CPU 版混淆交付。
- 项目目录与 CMake 工程名均使用实际名称 `emergences`。
- 后续可扩展：更多案例（群体行为、Lenia、CA）、规则「单源」共享、GPU 干预（把部分干预搬到 compute）。
