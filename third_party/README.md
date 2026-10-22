# 第三方依赖

此目录保存依赖说明。项目通过 CMake `FetchContent` 获取第三方源码，不将完整依赖复制进源代码仓库。

| 依赖 | 固定版本 | 用途 |
| --- | --- | --- |
| GLFW | 3.4 | 窗口、输入和 Vulkan surface 创建 |
| GLM | 1.0.1 | 模拟状态及后续相机所需的数学类型 |
| Dear ImGui | 1.91.9b | 后续实时参数面板与 GLFW / Vulkan 后端 |

依赖声明使用固定提交的源码归档，并以 SHA-256 校验下载内容。精确提交和哈希以项目的 CMake 依赖声明为准；升级版本时需同步更新提交和校验值。

实际依赖源码与构建产物位于所选构建目录的 `_deps` 子目录，例如默认预设的 `build/_deps`，或模拟接口预设的 `build/sim-only/_deps`。首次配置需要访问归档下载地址；已缓存的内容可供后续构建复用。第三方源码包中的许可证声明应随依赖保留。

Vulkan 不由 FetchContent 下载。应用构建使用本机安装的 LunarG SDK，由 `find_package(Vulkan)` 和 `glslc` 查找逻辑定位。`EMERGENCES_BUILD_APP=OFF` 的模拟接口构建只需 GLM，不需要 Vulkan SDK、GLFW 或 ImGui。
