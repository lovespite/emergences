# 涌现模拟软件（emergences）— 实现方案

## 实施进度（2026-10-22）

- 实际项目目录为 `D:\Projects\emergences`。
- 已建立 C++20 / CMake 构建、VS2022 预设、固定提交与校验值的 FetchContent 依赖、GLSL 构建期编译入口。
- 已建立 `core`、`sim`、`scenarios/boids` 三层目标；Window / Context 实现 GLFW 与 Vulkan 初始化，模拟层提供基础接口，场景提供应用入口。
- 已验证 LunarG Vulkan SDK 1.4.363.0、glslc、Khronos 校验层与 RTX 4060；模拟公共头文件独立编译及 Agent 布局检查通过。
- **Phase 1 已完成**：Swapchain / Pipeline / Buffer / Descriptor、Camera、ParticleRenderer / LineRenderer 和 ImGuiLayer 已接入。FrameRenderer 统一管理命令缓冲、双帧提交、每交换链图像的呈现信号量、缩放重建与最小化暂停；每幅交换链图像拥有独立深度附件。
- 默认程序是 2048 个程序化粒子的渲染预览，支持 0–20000 数量、箭头 / 圆点、朝向 / 速度 / 单色、大小、动画暂停 / 重置、网格 / 边界及轨道相机。鼠标被 ImGui 捕获时不驱动相机。当前运动由公式生成，Boids 模拟尚未实现。
- Debug、Release 完整构建通过；两种配置的 `camera_math`、`vulkan_triangle`、`vulkan_particles` 均为 3/3 通过。Debug 另以 `VK_LAYER_VALIDATE_SYNC=1` 检查，无校验诊断。
- 保留 12 帧三角形冒烟测试；新增至少 24 帧粒子冒烟测试，覆盖 0 / 1 / 257 / 8192 / 96 / 2048 粒子、缓冲扩容与缩减、模式 / 相机 / 面板和两次缩放，要求至少 3 代交换链。SDK 截图层已检查箭头、圆点、缩放画面，产物在 `build-host-x64/verification-phase1/`；未执行面板逐项点击的 UI 自动化。
- **下一步进入 Phase 2**：实现 CPU 模拟、空间哈希和 Boids 三规则；场景参数与光标干预在 Phase 3 完成，GPU compute 留待 Phase 4。模拟层当前仍是接口目标。

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

上图中的模拟后端与 Boids 规则是后续阶段的目标架构。**关键设计**：模拟层只产出「个体状态」（position / velocity / color）。渲染层通过独立的 `core::Particle` 记录消费「N 个朝向粒子」，当前预览直接生成这些记录。后续通过薄接口对接 CPU/GPU 后端：

- CPU 后端：`agents()` 返回 CPU 数组 → 渲染层每帧上传到 GPU 实例缓冲。
- GPU 后端：compute 直接写个体 SSBO → 渲染层把同一块 SSBO 当实例缓冲绑定（**零拷贝**）。

「同一套规则、两个后端」的实现策略（boids 规则各只有 3 行数学）：**C++ 与 GLSL 各写一份、保持同步**，规则数学在两侧注释互指。规则数量少时这是最省事、最透明的做法；若未来规则激增再引入「单源头文件 + 宏」方案。

---

## 目录结构

以下包含已落地的渲染层和后续模拟 / 场景文件规划；CPU 与 GPU 后端、Boids 规则和干预文件尚未实现。

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
│   │   ├── Camera.h/.cpp              # 轨道相机（旋转/缩放/平移）
│   │   ├── ParticleRenderer.h/.cpp    # 实例化朝向三角 + 点模式
│   │   ├── LineRenderer.h/.cpp        # 边界/网格；后续可扩展干预可视化
│   │   └── ImGuiLayer.h/.cpp          # ImGui + Vulkan/GLFW backend
│   ├── sim/                   # 通用模拟框架（不依赖 Vulkan）
│   │   ├── Agent.h                    # 个体状态
│   │   ├── World.h/.cpp               # 边界/包裹 + 邻域查询接口
│   │   ├── Rule.h                     # 规则接口
│   │   ├── Intervention.h             # 干预接口
│   │   ├── Backend.h                  # enum CPU / GPU
│   │   ├── Simulation.h               # 后端无关抽象
│   │   ├── ComputeDevice.h            # GPU 后端用到的薄设备接口（core 实现）
│   │   ├── cpu/CpuSimulation.h/.cpp   # CPU 后端 + 空间哈希
│   │   └── gpu/GpuSimulation.h/.cpp   # GPU 后端（Phase 4）
│   └── scenarios/boids/
│       ├── BoidsApp.h/.cpp            # 应用入口与三角形测试；后续组装模拟
│       ├── RenderPreview.cpp         # 已实现的粒子预览、ImGui 面板与冒烟测试
│       ├── Alignment.h/.cpp
│       ├── Cohesion.h/.cpp
│       ├── Separation.h/.cpp
│       ├── CursorRepel.h/.cpp         # 干预示例
│       ├── CursorAttract.h/.cpp       # 干预示例
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
| 颜色编码 | 预览已提供朝向色 / 速度色 / 单色；局部密度色在有真实邻域数据后加入 |
| 相机 | 轨道相机：左键拖旋转、滚轮缩放、中键平移；默认取景覆盖世界边界 |
| 世界边界 | 预览已绘制 `[-20,20]` 世界框和地面网格；模拟计划默认**周期包裹（torus）**，可选「硬墙反弹」 |
| 邻域查询(CPU，计划) | 均匀**空间哈希网格**，格宽 = 最大规则半径；每步重建，O(n) 均摊 |
| 积分（计划） | `vel += acc*dt` → clamp 速度上下限 → `pos += vel*dt` → 边界包裹 |
| 后端切换（计划） | `Backend` 枚举 + ImGui 下拉；CPU 挂干预，GPU 自行演化、大 N |

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
struct Neighbor { const Agent* agent; float distanceSquared; };
class Rule {
public:
    virtual ~Rule() = default;
    virtual glm::vec3 steer(const Agent& self, std::span<const Neighbor> nbrs,
                            const World& w) const = 0;
    virtual std::string_view name() const noexcept = 0;
    virtual float weight() const noexcept = 0;       // 后续 ImGui 可调
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

Boids 三规则即三个 `Rule` 子类（Alignment/Cohesion/Separation），各自带 weight/radius，在 ImGui 里分别可调、可开关 —— 直接演示「设定行为/数量/规则」。

干预示例：`CursorRepel`（鼠标射线投影到平面→半径内个体被推开）、`CursorAttract`（同理吸引）。二者作为「干预」接口的落地验证，可在 ImGui 开关。

---

## 分阶段实施

### Phase 1 — Vulkan 渲染框架（core）+ 冒烟测试（已完成）

1. `CMakeLists.txt`：C++20、`FetchContent`(GLFW/GLM/ImGui)、`find_package(Vulkan)`、`glslc` 着色器编译函数、Debug 配置开启校验层。
2. `Window` / `Context` / `Swapchain` / `Pipeline` / `Buffer` / `FrameRenderer` / `Descriptor`。命令记录与提交统一由 `FrameRenderer` 管理，无独立 `CommandBuffer` 包装类。
3. **三角形冒烟测试**（已完成）→ 打通 instance→present 全链路，自动呈现 12 帧并验证两次窗口缩放。
4. `ParticleRenderer`（从 CPU 数组画 N 个朝向三角 / 圆点）、`LineRenderer`（边界/网格）、`Camera`、`ImGuiLayer`；程序化预览与粒子 / 相机测试已通过。

### Phase 2 — 模拟框架 + CPU 后端（下一阶段）

5. `Agent/World/Rule/Intervention/Simulation/Backend` 抽象已建立；补齐实现并验证规则、邻域和边界语义。
6. `cpu::CpuSimulation`：空间哈希邻域、积分、周期边界包裹。
7. Boids 三规则 `Alignment/Cohesion/Separation`。

### Phase 3 — Boids 场景 + 交互

8. `BoidsApp`：sim→renderer 对接、每帧上传实例数据、主循环。
9. ImGui 面板：个体数量、每规则 weight/radius、速度上下限、暂停/重置、后端切换（GPU 先灰显）、颜色模式。
10. 干预 `CursorRepel/Attract`，ImGui 开关。

### Phase 4 — GPU 后端（compute）

11. 计算管线基建：compute shader、SSBO、dispatch、`ComputeDevice` 接口（core 实现）。
12. `boids.comp`：三规则 GLSL + **均匀网格**（cell→agent 列表排序 + 前缀和，查询 27 邻域）。
13. `gpu::GpuSimulation`：compute 写个体 SSBO，渲染层零拷贝绑定；ImGui 后端切换可用（CPU=带干预 / GPU=大 N 自行演化）。

**执行顺序承诺**：先完成 Phase 1–3 得到一个可运行、可干预的鸟群 demo，验证后再进 Phase 4。

---

## 验证方式

1. **当前构建与自动检查**：`cmake --preset vs2022 -DEMERGENCES_ENABLE_GPU_TESTS=ON`，随后构建 `debug` / `release`。Debug 使用 `ctest --preset debug`，Release 使用 `ctest --test-dir build-host-x64 -C Release --output-on-failure`；两者均须通过 `camera_math`、`vulkan_triangle` 和 `vulkan_particles`。需先装 LunarG SDK，确保 `VULKAN_SDK` 可用。
2. **Phase 1 当前验收**：运行 `build-host-x64\Debug\emergences.exe`，查看公式生成的粒子预览、箭头 / 圆点、网格、边界和面板；`--render-smoke-test` 覆盖粒子数量 / 缓冲变化、相机、UI 绘制和缩放重建。截图检查已完成，面板鼠标操作仍可按 README 中的说明进行交互复查。
3. **校验层**：Debug 运行无 `VK_LAYER_KHRONOS_validation` 报错；可设置 `VK_LAYER_VALIDATE_SYNC=1` 加验同步。
4. **Phase 2–3 后续验收目标**：接入真实 Boids 后确认：

   - 窗口出现，N 只三角鸟随时间自发「聚拢 + 对齐 + 分离」，形成可见鸟群/涡旋。
   - ImGui 里拖动「分离/对齐/聚拢」权重、半径，行为实时变化；调数量 N 平滑增减。
   - 打开「光标排斥」干预，鼠标划过鸟群被推开；关闭后恢复自行演化。
   - 切换颜色模式，看到朝向/密度分布。

5. **Phase 4 后续目标**：ImGui 切到 GPU 后端，验证 10 万级个体的性能与 CPU 版行为一致性（同规则、同参数）；当前尚未实现或测得此规模性能。

---

## 风险与后续

- **Vulkan SDK 是硬前置**：本机已安装并验证，其他机器仍需完整 SDK。
- GPU 后端的「均匀网格」compute 是标准但中等复杂度的部分，故单列为 Phase 4，不与 CPU 版混淆交付。
- 项目目录与 CMake 工程名均使用实际名称 `emergences`。
- 后续可扩展：更多案例（群体行为、Lenia、CA）、规则「单源」共享、GPU 干预（把部分干预搬到 compute）。
