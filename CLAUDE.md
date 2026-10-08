# AGENTS.md

本文档适用于本仓库根目录及其全部子目录。后续 agent 在修改代码、文档、测试、工程文件或发布配置前，应先阅读本文档；若更深层目录以后增加自己的 `AGENTS.md`，以更深层文件的约定为补充或覆盖。

## 项目定位

`plugin.geometry2d` 是 Solar2D 原生几何插件，Lua 入口为：

```lua
local Geometry2D = require("plugin.geometry2d")
```

插件属于以下两类的组合：

- 直接 Lua C 模块：由 `luaopen_plugin_geometry2d()` 直接创建并返回模块表。
- 图形和内存集成插件：生成 `display.newMesh()` 数据，并通过 `CoronaMemoryInterface` 暴露只读 packed buffer。

当前架构不需要且不应擅自引入：

- Lua facade、`lua2c` 或提交生成的 Lua bytecode；
- `CoronaLibraryNewWithFactory()`；
- external texture、平台生命周期 delegate；
- WebM、`third_party/solar2d_native_utils`、`ByteReader` 等旧插件模板依赖。

唯一公开原生入口必须保持为 `luaopen_plugin_geometry2d`。不要为了套用其他 Solar2D 插件模板而改变绑定模型。

## 公共 API 与文档来源

- `docs/api.md` 是公共 Lua API、默认值和示例的权威文档。
- `src/shared/plugin_geometry2d.cpp` 只负责注册七个子模块：`polypartition`、`earcut`、`fringe`、`util`、`path`、`clipper2`、`ribbon`。
- 任何公共 API、参数、默认值、返回结构或错误契约的改动，都必须同步更新 `docs/api.md` 和对应测试。
- 参数按函数严格限定。未知参数、无关参数和旧拼写不得静默忽略。
- 角连接参数固定为 `join`，不要重新接受 `joint` 别名。

本插件尚未大规模使用，因此必要时可以及时修正不合理 API；但修改必须明确、一次完成，并同时清理文档、测试和旧示例，不能留下两个含义相近的长期别名。

## 目录与职责

- `src/shared/plugin_geometry2d.cpp`：仅模块注册，保持短小。
- `src/shared/geometry2d_lua.{h,cpp}`：Lua 输入解析、公共类型、选项白名单和参数验证。
- `src/shared/polypartition_module.cpp`：polypartition Lua API。
- `src/shared/earcut_module.cpp`：earcut Lua API。
- `src/shared/fringe.{h,cpp}`：从 NanoVG 思路抽取并重写的 fill/stroke fringe、join 和 cap 网格生成。
- `src/shared/bezier_path.cpp`：Bezier 命令解析、曲线自适应离散和 path API。
- `src/shared/clipper2_bridge.{h,cpp}`：path fill 的交错检测、fill rule 归一化，以及与现有 mesh 构建流程的衔接。
- `src/shared/clipper2_module.cpp`：独立 `clipper2` Lua 子库绑定；不得在这里加入实验性的 triangulation。
- `src/shared/mesh_builder.{h,cpp}`：fill、SDF、stroke、dash 的组合网格构建。
- `src/shared/sdf_builder.{h,cpp}`：SDF v2 全局最近边界距离分区、内部饱和区及外侧 AA 范围；不依赖 Lua 或 stencil。indexed/triangles 共用几何结果。
- `src/shared/earcut_stroke_builder.{h,cpp}`：earcut 近似内描边，局部 miter 内外带＋核心三角化；不做全局距离场或组间并集，输出 approximate=true。
- `src/shared/mesh_result.{h,cpp}`：table、CoronaMemory buffer、直接 display mesh 三类输出。
- `src/shared/corona_buffer.{h,cpp}`：本插件自有的 owning readable buffer userdata。
- `src/shared/ribbon_builder.{h,cpp}`：有状态 ribbon 点队列、核心/外侧 AA 几何及容量复用。
- `src/shared/ribbon_module.cpp`：ribbon Lua 绑定、借用 buffer 生命周期与 retained view 更新。
- `tests/ribbon_simulator/`：独立 ribbon Simulator 测试；`tests/ribbon_native/` 的 mock 绑定检查不能代替渲染测试。
- `examples/solar2d/visual_tests.lua`：视觉显示测试和示例场景。
- `examples/solar2d/assert_tests.lua`：API 契约与行为断言。
- `examples/solar2d/main.lua`：只加载上述两个测试文件，保持很小。

不要重新把各模块实现堆回 `plugin_geometry2d.cpp`。新增独立算法或较大的绑定逻辑时，优先创建职责单一的 shared translation unit，并把它加入所有实际支持的平台工程。

## C++ 与 Lua 绑定规范

- shared 代码以 C++17 为基线（Clipper2 2.x 的头文件和 offset 实现需要 C++17）；新增代码必须能在 macOS、iOS、tvOS、Android NDK 和 MSVC v143 的 C++17 模式编译。
- 不使用平台私有类型实现公共几何算法。平台差异应留在各平台工程或构建脚本中。
- 所有来自 Lua 的坐标、宽度、容差、offset 等浮点数都要拒绝 NaN、Infinity 和超出 `float` 范围的值。
- 数组型参数需要验证为 dense array；不要只依赖 `lua_objlen()` 而接受额外或稀疏条目。
- 在继续 push Lua 值前，用 `CoronaLuaNormalize()` 固定需要长期使用的相对栈索引。
- 每个绑定函数必须清楚维护 Lua 栈，并准确返回压入的结果数量。
- 调用 `display.newMesh()` 等 Lua API 时使用受保护调用；构造失败返回 `nil, message`，不要让临时 display object 或辅助 descriptor 泄漏。
- `CoronaMemory` 输出必须由 userdata 自己拥有字节内容；不能暴露指向临时 `std::vector`、栈内存或调用结束后失效的指针。
- 唯一例外是 `ribbon:snapshot("buffers")` 的明确借用协议：数组归有状态 generator 所有，descriptor 不延长 generator 生命周期；几何变化/销毁后必须拒绝旧 buffer。不能将此协议套用到 stateless mesh API。
- descriptor 中的 `count` 表示元素数量，不是字节数；自定义顶点属性 descriptor 需要正确设置 `componentCount`。

## 输入与坐标约定

- 坐标使用 Solar2D 屏幕空间：x 向右，y 向下。
- signed shoelace area 为正时，轮廓在屏幕上顺时针，默认视为 outer。
- area 为负时，轮廓在屏幕上逆时针，默认视为 hole。
- 显式 `{points=..., hole=true/false}` 优先于 winding 推断。
- earcut 自行处理 winding，不要人为要求调用者先翻转。
- packed 坐标输入必须写成带类型的 descriptor：`{bytes=..., type="float32"}`，也可为 `float64` 或 `int32`。
- packed 数值使用平台 native byte order；不要根据字节长度猜测标量类型。

Bezier path 当前只支持绝对命令：`M/L/Q/C/Z` 及文档中的 long name。不要把小写相对命令误当作绝对命令。新增命令时必须定义精确参数个数、错误行为、flatten 规则和测试。

path fill 默认使用 `fillRule="nonZero"`、`intersections="error"`；遇到轮廓自交或轮廓之间相交时返回 `nil, message`。只有调用者明确指定 `intersections="resolve"` 时才通过 Clipper2 先归一化轮廓。`fillRule="evenOdd"` 也需要先归一化嵌套关系，但不能因此静默绕过默认的交错报错策略。

## 错误契约

必须区分两类失败：

1. 调用契约错误使用 Lua error：类型错误、格式错误、未知选项、非法枚举、非有限数值、错误的 path command。
2. 输入合法但算法无法得到可用几何时返回 `nil, message`：三角化失败、partition 失败、退化轮廓、超过网格限制等。

不要把正常可处理的几何失败改回直接抛错。`polypartition`、`earcut`、`fringe`、`util` 和 `path` 都应遵守同一原则。

`output="mesh"` 成功返回 `displayMesh, attributes`；构造或几何失败返回 `nil, message`。

## Mesh 输出约定

三种输出模式必须保持一致：

- `output="table"`：默认兼容模式，返回 Lua number table；`alphas` 可逐顶点使用。
- `output="buffers"`：返回 owning CoronaMemory descriptor。
- `output="mesh"`：插件调用 `display.newMesh()`，并把 packed 辅助属性作为第二返回值。

其他固定约定：

- indexed table 输出使用 Solar2D 的 1-based indices。
- indexed buffer 输出使用 packed zero-based `uint16_t`，并设置 `zeroBasedIndices=true`。
- indexed mode 最多 65535 个顶点；更大结果应返回 `nil, message` 并提示使用 `mode="triangles"`。
- triangle-list 顶点数必须能被 3 整除，且不返回 indices。
- `output="mesh"` 不自动执行 `mesh:translate(mesh.path:getVertexOffset())`；对象定位由调用者处理。
- SDF 输出使用 float32 `distances`，不要自动绑定 shader 或 vertex extension。
- 距离入口固定为 meshDistance / meshDistanceGroups，始终输出 distances。method="local"（默认）或 "partition"，不再接受 meshSDF 别名、backend、geometry。partition 允许 innerRange=0（外侧距离带、内部零），local 要求正内宽。使用 innerRange=0、distanceTolerance 或 distanceTransform 必须显式选择 partition，不自动切换方法。距离符号、UV 与生命周期规则不变。
- 普通 meshFill 使用 aa="none" / "vertex"（默认）与 aaWidth；topology="direct"（默认）/ "normalize" 独立控制填充区域归一化。path 的 intersections="error" 不因 topology 而绕过。普通 meshStroke 为居中描边，使用 aa/aaWidth。低层 fringe 和 ribbon 参数不随之更名。
- 距离 shader 依据 kind="distance" 与 innerRange==0 选择分支；fill 不绑定距离 shader。详见 docs/api.md。


## 顶点 Alpha、颜色与 Solar2D 源码约定

距离 method="local" 为局部 miter 近似距离带，返回 approximate=true，不保证误差上界或组间无重叠；partition 仍可能在 float32 输出中失败。API 改名不是几何修复。无 AA 的 fill/stroke 不生成 alphas 或 fillVertexColors；fill 总是输出基于原始 bounds 的材质 UV。禁止在失败时静默换算法、舍弃三角形或启用局部修复实验。

fringe/stroke AA 的 `alphas` 是插件生成的每顶点覆盖率：实体边为 1，fringe 外缘为 0。它不是 ThorVG fringe，也不依赖自定义 AA shader。

对于 alpha mesh：

- table 输出保留 float `alphas`，调用者可使用 `setFillVertexColor()`。
- buffer 输出额外提供 packed RGBA8 `fillVertexColors`；RGB 固定为白色，A 为 clamp 并量化后的 alpha。
- `mesh:setFillColor(r, g, b)` 用于整体 tint，不能破坏原 AA alpha ramp。
- 当前目标 Solar2D 源码支持 `display.newMesh()` 在初始化时直接消费 `fillVertexColors` descriptor。不要在创建后再自动调用一次 `mesh.path:update()`。
- `output="mesh"` 传给构造器的 descriptor 与 `attributes.fillVertexColors` 应复用同一个 Lua descriptor，避免重复生成 buffer。
- `mesh.path:update({fillVertexColors=...})` 只用于创建后的动态更新，或由旧 Solar2D 调用者手动兼容。

相关引擎回归用例位于：

- `<CORONA_ROOT>/test/mesh_fill_vertex_colors_initialization`
- `<CORONA_ROOT>/test/bugs/mesh_fill_vertex_colors_buffer_update`

当前 Solar2D 还修复了 vertices buffer 存在但 UV buffer 缺失时的空指针问题。默认不要生成冗余 UV；`legacyUVs=true` 是旧引擎兼容选项，仅允许用于 buffer/mesh 输出。

`fillExtendedData:setAttributeValues()` 可直接消费本插件的 packed `distances`。在当前引擎中，新 mesh 的 render geometry 要到首次渲染后才具备有效 vertex count，因此新对象的 bulk extended-data 写入应等待一次完成的渲染；不要把成功编译误当作该运行时路径已验证。

## 几何质量和性能边界

- `tessTol` 越小，Bezier、round join 和 round cap 的顶点数越多。
- `join="round"`、`cap="round"` 和短 dash pattern 都会明显增加顶点数。
- 每个可见 dash 通常带来独立 caps；必须保留 `maxDashSegments` 限制，不能生成无界顶点。
- `maxCurvePoints` 是每个 contour 的防护上限，不能绕过。
- dash 按 flatten 后的 polyline 长度计算；曲线精度仍由 `tessTol` 决定。
- 尽量把多个兼容形状合并为一个插件结果，但不要声称这等于 Solar2D renderer 一定只产生一个 draw call。
- `mode="triangles"` 会复制共享顶点，可能快速增大 CPU、内存和上传成本。
- Solar2D triangle-list batching 只合并兼容的非索引 triangles，且引擎功能默认关闭。插件不能假设 batching 已启用，也不能依赖它保证性能。
- 在性能敏感路径优先使用 CoronaMemory 批量接口，避免 Lua 逐顶点 setter；但不要为了少量复制引入悬空内存或复杂生命周期。

## 第三方代码与许可证

实际构建依赖：

- `third_party/polypartition`：git submodule，partition/部分 triangulation。
- `third_party/earcut`：git submodule，mapbox earcut。
- `third_party/clipper2`：git submodule，路径布尔运算、offset、矩形裁剪和 Minkowski；暂不构建其 triangulation。
- `third_party/nanovg`：git submodule，算法来源和参考；插件只维护抽取、改写后的相关代码。

`third_party/thorvg` 当前不是 `.gitmodules` 中的构建依赖，也没有加入平台工程。除非任务明确要求，不要把完整 ThorVG renderer、scene graph 或 raster backend 接入插件。

修改第三方派生算法时：

- 保留源文件中的来源、许可证和关键差异说明；
- 只抽取完成目标所需的最小逻辑；
- 不直接编辑 submodule 内容来掩盖插件侧问题；
- 不复制与本插件无关的渲染、WebM、图片解码或平台辅助代码。

克隆仓库后使用：

```sh
git submodule update --init --recursive
```

## 平台与产物

平台工程应引用同一组 `src/shared` 源文件。新增或删除 shared translation unit 时，必须同步检查 macOS、iOS、tvOS、Android 和 Windows 工程。

- macOS Simulator：`plugin_geometry2d.dylib`，Release Universal `arm64 + x86_64`。
- iOS：`libplugin_geometry2d.a`，分别打包 device 与 simulator。
- tvOS：`Corona_plugin_geometry2d.xcframework`。
- Android：`libplugin.geometry2d.so`，当前配置包含 `armeabi-v7a`、`arm64-v8a`、`x86`、`x86_64`，Android API 21。
- Windows Simulator：`plugin_geometry2d.dll`，Release/Win32，MSVC v143。

各平台构建脚本默认输出到 `plugins/2025.3720/<platform>`，也接受 CI 通过 `PLUGIN_BUILD` 覆盖版本目录。更换 Solar2D ABI/build 时，必须同时检查所有脚本、metadata、CI 环境变量和目录名，不能只改其中一处。

`.github/workflows/publish.yml` 必须从源码生成 macOS、iOS、tvOS、Android 和 Win32 的发布产物，不能静默复用仓库里已有的二进制。Android 构建和发布校验都必须保留 `armeabi-v7a`、`arm64-v8a`、`x86`、`x86_64` 四个 ABI；不得因为当前设备常用 ARM 而删掉 x86 产物。`workflow_dispatch` 只编译和上传 CI artifact，不创建 release，便于安全验证 workflow。

Windows 工程已移除旧 WebM 配置。本地 macOS 环境不能验证 Windows；只修改 `.vcxproj`、`.filters`、`.sln` 或 `.bat` 时，要明确报告“静态修改，Windows 未编译”，不能声称 Windows 构建通过。GitHub Actions 会安装对应 Solar2D MSI、定位 Native SDK 并使用 MSVC Win32 工具链；在真实 runner 成功前仍只能视为 CI 配置待验证。

## 常用验证命令

先做低成本检查：

```sh
git diff --check
luac -p examples/solar2d/main.lua \
  examples/solar2d/visual_tests.lua \
  examples/solar2d/assert_tests.lua
```

macOS 插件完整构建、安装并运行最新本地 Simulator：

```sh
cd src/mac
./start_simulator.sh
```

复用已构建插件，只重新运行 Lua 示例：

```sh
cd src/mac
./start_simulator.sh --skip-build
```

开发路径统一配置在不入库的 `dev.local.json`；字段及覆盖规则见 `docs/development.md`。`start_simulator.sh` 只编译并调用统一 runner 的交互模式，不同步本地服务器；测试模式使用 `tests/run_simulator.py`。

构建后至少检查：

```sh
lipo -archs src/mac/build/Release/plugin_geometry2d.dylib
nm -gU src/mac/build/Release/plugin_geometry2d.dylib | rg luaopen_plugin_geometry2d
```

其他平台入口：

- `src/ios/build.sh`
- `src/tvos/build.sh`
- `src/android/build.sh`
- Windows Developer Command Prompt 中执行 `src\win32\build.bat`

只有环境可用且实际执行成功后，才能报告对应平台已编译。Simulator 通过不能代替 iOS/tvOS device、Android device 或 Windows runtime 验证。

## 测试要求

- API/算法改动至少增加或更新 `assert_tests.lua` 中的断言。
- 影响可视轮廓、AA、join、cap、dash、winding 或颜色的改动，同时更新 `visual_tests.lua`。
- 视觉测试只负责展示和人工观察；可自动判断的契约放在 assert 测试。
- 运行时测试应覆盖 table、buffers、mesh 中受影响的输出形式。
- 涉及 packed buffer 时，至少验证 `buffer`、`count`、`componentCount` 和顶点数量一致；颜色路径应尽可能用 `display.colorSample()` 验证实际渲染，而不是只断言字段存在。
- 异步测试要明确等待的帧数或渲染阶段，并在完成后移除 listener/display object。
- 测试入口发生 Lua error 时，应输出 `debug.traceback`，不能只留下无行号的 Simulator panic。
- 独立 ribbon 测试入口为 `python3 tests/run_simulator.py tests/ribbon_simulator --plugin src/mac/build/Release/plugin_geometry2d.dylib`。runner 传入绝对项目目录和 `-ApplePersistenceIgnoreState YES`，避免崩溃恢复窗口阻塞；只在明确结果标记后报告通过。
- 当前引擎的 `display.colorSample()` 回调注册假定主 Lua state。协程测试应把采样请求交给主 `enterFrame` listener，再将结果返回协程。
- crash 回归应先查看 `crashes/` 中已有记录，新增最小复现和修复说明。

提交验证结果时要区分：

- 静态检查通过；
- 编译/链接通过；
- 插件成功加载；
- Simulator 运行时断言通过；
- 真机或其他平台尚未验证。

## Git 与变更卫生

- 工作树可能包含用户已有的修改、未跟踪测试文件和 submodule 状态；不要清理、覆盖或回退无关内容。
- 未经明确要求，不要 `git add`、commit、修改 submodule revision 或生成发布 release。
- 不要使用 `git reset --hard`、`git checkout --` 等破坏性命令。
- 修改工程文件时只加入本次需要的 shared 源文件，不要顺便恢复旧 WebM 或无关模板配置。
- `plugins/**` 是生成目录，不纳入 Git；metadata 源文件保留在 src。原始日志、crash dump、私有业务 fixture 和本机配置也不提交。
- 普通分支 push、PR 和手动 workflow 只检查/编译；仅显式推送与 VERSION 一致的 vN（N 为无前导零的正整数，如 v1、v2） tag 才能发布。示例固定版本也须一致；不得自动递增版本、覆盖已有 Release 或未经授权创建/push tag。详见 `docs/releasing.md`。

## 完成标准

一次实现只有在以下事项与任务范围相符时才算完成：

- shared 实现和所有相关平台工程保持一致；
- Lua 栈、内存所有权、错误契约和旧 API 兼容性已检查；
- `docs/api.md` 与实际行为一致；
- assert/视觉测试按影响范围更新；
- 静态检查通过；
- 本地可用平台完成编译和必要运行时验证；
- 未验证的平台被准确标注，而不是推测通过；
- 没有改动或删除用户的无关工作树内容。
