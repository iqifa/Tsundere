# 后处理参数与视口预览

## 后处理

从主菜单 **Window > Post Processing > Parameters...**，或视口的
**Post processing...** 按钮打开面板。用 **Effect** 选择效果，**Enabled** 控制启用。
主菜单切换效果时也会选中对应的参数页。参数修改在下一帧生效。

- Bloom：阈值、软阈值、强度、半分辨率模糊半径。
- Blur：像素半径。
- Depth of Field：焦距、离焦范围、最大模糊像素半径。
- Motion Blur：快门强度、最大拖影长度。
- Tone Mapping：曝光 EV、显示 gamma。
- Color Grading：饱和度、色调乘数。
- Sharpen、Chromatic Aberration：强度 / 像素偏移。
- Vignette：强度、内外半径。
- FXAA：相对/最低边缘阈值、最大采样跨度。

Reset all parameters 会恢复全部参数和效果开关的默认值。设置目前属于当前编辑器会话。
后处理内部仍使用 RGBA16F，视口预览不会改变材质/GBuffer 的输出协议。

## 视口

**Output** 选择最终颜色、后处理前颜色、法线与 shadingModel、baseColor 与 AO、
metallic/roughness、速度、自发光或深度。当前渲染路径不提供选中的输出时，显示提示并回退到最终颜色。

**Format** 是实际 GPU 输出附件格式转换：RGBA8 UNORM、R8 UNORM、R16F、RG16F、
RGBA16F、RGBA32F。源格式和转换格式会显示在控件下方。
UNORM 会把值限制到 [0,1] 并量化；浮点格式可以保留负值和 HDR。
R 格式仅保留 R，RG 仅保留 R/G；缺失的 G/B 采样为 0，缺失 A 采样为 1。

**Channel** 支持 RGB、RGBA、R、G、B、A；单通道以不透明灰度显示。
例如：法线的 A 是 shadingModel；baseColor 的 A 是 AO；metallic/roughness 的 R/G
分别是金属度/粗糙度。查看 R16F 时选择 R，可以得到灰度预览。

**Display range** 把指定数据区间映射到黑/白，仅影响显示，不修改存储数据。
法线可使用 [-1,1]；HDR 可扩大上界；深度为原始 device depth，而非线性距离。
预览采用独立的格式转换和显示两个全屏 pass，避免通道显示先被 R/RG 目标丢弃。

## 验证

在仓库根目录运行 `tests\RunPostProcessPreviewTests.cmd`：

- 编译 OpenGL 和 Vulkan 两套 shader，检查 32/16 字节 push constant 布局。
- 使用离屏 OpenGL 4.3 检查曝光、Bloom 阈值/强度、饱和度、零强度效果。
- 对 6 种真实 GPU 附件格式逐个检查截断、精度、缺省通道。
- 检查每种格式的 6 种通道显示，以及显示范围与 alpha 行为。

Debug 工程构建通过。Vulkan 已验证编译，未进行 Vulkan 界面运行时验收。
