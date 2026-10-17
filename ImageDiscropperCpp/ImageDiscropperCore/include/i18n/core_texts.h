// ============================================================================
// 文件：include/i18n/core_texts.h（Core 常量层 · 字符串）
// 作用：把散落在 Core 各模块的硬编码字符串收编为命名常量，供人工核对/调整，并作为 i18n 的源语言层。
//       本文件三个顶层分组**可译性相反**，用组名显式标记：前两组整组保持原样，只有第三组进语言目录：
//       · jsonkey  —— 不可译：SPEC §7 JSON 键名（协议标识串），改动任意一条都会破坏配置文件的向后兼容。
//       · defaults —— 不可译：§7 缺字段时的回退值、通道掩码与命名 token——它们同样是要写进文件的数据，不是给人读的文案。
//       · msg      —— 可译：面向人的错误 / 校验报因与图案显示名，使用点经 IDC_KTR / IDC_KTRF 取译文（见 core_i18n.h）。
// 分块依据：jsonkey 按 SPEC §7 的段归属（每组注释标明实现来源文件，即这些键的实际读写处）：
//       · common —— 跨段复用的同名键（width/height/x/y/kind/opacity/…）只定义一次，
//         避免同一键名在写出侧与读取侧各写一份而漂移（这是本文件存在的核心理由）。
//       · config —— §7 顶层与 source/cut/select/order/emit/merge/arrange 段（src/engine/engine_config_json.cpp）。
//       · layer  —— generate 段与 layers[i] 的 source/transform/imagePlacement（同上 + engine_config_json_internal.h）。
//       · anno   —— annotations 段逐条元素（src/annotation/annotation_json.cpp）。
//       · pre    —— 预处理步骤 {op, ...params}（engine_config_json_internal.h / engine_config_json.cpp）。
// 命名：k + 键名驼峰（cellWidth → kCellWidth）；值与 SPEC §7 逐字节一致，注释写「谁写 / 谁读」。
// 类型：inline constexpr char[]（UTF-8 字节串，MSVC 三层已 /utf-8）。nlohmann 的 operator[] 与
//       jsonutil::at(const char*) 直接接受，不引入 std::string 全局对象（免静态初始化顺序与启动开销）。
// 提取判据（与另五文件同）：键名一律提取；§7 回退默认值按「出厂默认」提取；枚举 ↔ 串的**成对映射表**
//       内值串不提取（映射表本身即该 token 的唯一定义点，如 "row-major" / "keep-partial"，再抽常量只会增加跳转）；
//       例外：面向人的**显示名**不属此列——generate::patternKindName 虽也是枚举分派，但它返回的中文要进 GUI
//       下拉与提示（落盘用的是 defaults 的 patternKind 串与 gen*Token 两套无关的 token），故已提取进 msg 组。
//       msg 内由多段拼接而成的句子一律收为**单条整句 + %N 占位符**（如 "第 %1 个标注元素：%2"），
//       不拆成连接片段——拆开后译者无法重组语序；只以「'」收尾的前段仍只存前段，
//       后接的原值与尾引号由调用点拼接（那部分是数据，不是句子成分）。
// 目录与对账：msg 的英文表住 src/i18n/lang_en.cpp，由 GUI / CLI 各自的 installCatalogs() 经
//       installCoreCatalogs() 装配（三层共用 Core 的一张进程表，键 = `分组::常量名`）；故
//       **跨层同名键**（GUI 与 Core 都写 msg::kX）会让后装者永不可见——这类静默失效由
//       tests/test_i18n_catalog.cpp 断言拦下（同表还查孤儿键 / 重复键 / 契约组越界并打印覆盖率）。
// ============================================================================
#pragma once

namespace idc::core::texts::jsonkey {

// ---------------------------------------------------------------------------
// common：跨段复用的同名键（写出侧与读取侧共用同一定义）。
// ---------------------------------------------------------------------------
namespace common {
inline constexpr char kWidth[] = "width";             // 原图尺寸 / 图层变换 / resize 步 / cellSize 兼容对象。
inline constexpr char kHeight[] = "height";           // 同上，纵向。
inline constexpr char kX[] = "x";                     // 图层变换落位 / 内层落位 / 文字标注落点。
inline constexpr char kY[] = "y";                     // 同上，纵向。
inline constexpr char kX1[] = "x1";                   // 矩形锚点左上 / 标注双锚点第一点。
inline constexpr char kY1[] = "y1";                   // 同上。
inline constexpr char kX2[] = "x2";                   // 矩形锚点右下 / 标注双锚点第二点（圆/椭圆编码半轴）。
inline constexpr char kY2[] = "y2";                   // 同上。
inline constexpr char kW[] = "w";                     // merge.cellSize 的 {w,h} 兼容写法。
inline constexpr char kH[] = "h";                     // 同上。
inline constexpr char kType[] = "type";               // 标注形状类型（annotations[i].type）。
inline constexpr char kKind[] = "kind";               // 图案类型（generate.kind）/ 图层来源（layers[i].source.kind）。
inline constexpr char kOpacity[] = "opacity";         // §7 顶层画布级 / 图层与底图合成期 alpha 乘子。
                                                  // 与 defaults::kPreStepOpacity 同字不同义（现行键名 / 已废弃旧步名哨兵），各立一条。
inline constexpr char kPreprocess[] = "preprocess";   // §7 顶层、图层、底图三处同名的步骤数组。
inline constexpr char kTransform[] = "transform";     // 图层变换段 / 标注几何变换段（同名不同义，值仍一致）。
inline constexpr char kZOrder[] = "zOrder";           // 图层叠放次序 / 标注叠放次序。
inline constexpr char kFill[] = "fill";               // 标注是否填充（图层落位的适应方式键是 fit，见 layer::kFit）。
inline constexpr char kClosed[] = "closed";           // 折线 / 约束多边形闭合标志。
inline constexpr char kPoints[] = "points";           // 直线 / 折线 / 多边形顶点数组。
inline constexpr char kCornerRadius[] = "cornerRadius"; // 多边形圆角半径（约束与自由多边形共用）。
inline constexpr char kFile[] = "file";               // 图层来源的相对文件名（位图恒不入配置，只存名）。
} // namespace common

// ---------------------------------------------------------------------------
// config：§7 顶层与 cut / select / order / emit 段（src/engine/engine_config_json.cpp）。
// ---------------------------------------------------------------------------
namespace config {
inline constexpr char kSource[] = "source";           // 顶层：原图尺寸段。
inline constexpr char kCut[] = "cut";                 // 顶层：切割段（层级 / 生成器 / 几何 / 极性）。
inline constexpr char kSelect[] = "select";           // 顶层：网格模式显式选择集（单元序号数组）。
inline constexpr char kOrder[] = "order";             // 顶层：排序策略段。
inline constexpr char kEmit[] = "emit";               // 顶层：输出段。
inline constexpr char kGenerate[] = "generate";       // 顶层：底图描述段（键名与 CLI 选项名同源，属契约不可改）。
inline constexpr char kLayers[] = "layers";           // 顶层：多层画布数组。
inline constexpr char kAnnotations[] = "annotations"; // 顶层：标注数组。
inline constexpr char kTier[] = "tier";               // cut.tier（"extract"/"erase"/"grid"）。
inline constexpr char kGenerator[] = "generator";     // cut.generator。
inline constexpr char kPolarity[] = "polarity";       // cut.polarity（keep/remove）。
inline constexpr char kParams[] = "params";           // cut.params（随 generator 形态异构）。
inline constexpr char kRects[] = "rects";             // cut.params.rects（MultiRect）。
inline constexpr char kStrategy[] = "strategy";       // order.strategy / emit.merge.arrange.strategy。
inline constexpr char kReverse[] = "reverse";         // 逆序标志（两处同名字段）。
inline constexpr char kSnake[] = "snake";             // 蛇形标志（两处同名字段）。
inline constexpr char kSequence[] = "sequence";       // 自定义序预留位（写出恒 null）。
inline constexpr char kMode[] = "mode";               // emit.mode（merged/separate）。
inline constexpr char kMerge[] = "merge";             // emit.merge 段。
inline constexpr char kFormat[] = "format";           // emit.format。
inline constexpr char kNaming[] = "naming";           // emit.naming（命名模板串，值为数据非文案）。
inline constexpr char kQuality[] = "quality";         // emit.quality。
inline constexpr char kKeepMetadata[] = "keepMetadata"; // emit.keepMetadata。
inline constexpr char kLayout[] = "layout";           // emit.merge.layout（collapse/rearrange）。
inline constexpr char kCols[] = "cols";               // emit.merge.cols。
inline constexpr char kRows[] = "rows";               // emit.merge.rows。
inline constexpr char kCellWidth[] = "cellWidth";     // emit.merge.cellWidth / grid.cellWidth（同名复用）。
inline constexpr char kCellHeight[] = "cellHeight";   // 同上，纵向。
inline constexpr char kPadColor[] = "padColor";       // emit.merge.padColor（十六进制串）。
inline constexpr char kArrange[] = "arrange";         // emit.merge.arrange 段（重排填充顺序）。
inline constexpr char kCellSize[] = "cellSize";       // **兼容读取专用**：SPEC §7 示例的旧合并写法。
} // namespace config

// ---------------------------------------------------------------------------
// layer：generate 段（底图）与 layers[i]（图层）的 source / transform / imagePlacement
//       （src/engine/engine_config_json.cpp + engine_config_json_internal.h）。
// ---------------------------------------------------------------------------
namespace layer {
inline constexpr char kRecipe[] = "recipe";           // source.recipe（图案配方）。
inline constexpr char kBackground[] = "background";   // 图案背景色（十六进制串）。与 GUI palette 键名 gui_texts::ident::kPaletteBackground 同字纯属巧合，不是同一个值。
inline constexpr char kForeground[] = "foreground";   // 图案前景色。
inline constexpr char kOrientation[] = "orientation"; // 图案方向（gradient/stripes 用）。
inline constexpr char kPeriod[] = "period";           // 图案周期（stripes/checkerboard 等）。
inline constexpr char kLineWidth[] = "lineWidth";     // 图案线宽。
inline constexpr char kDotRadius[] = "dotRadius";     // 图案点半径。
inline constexpr char kSeed[] = "seed";               // 噪声随机种子。
inline constexpr char kNoiseMode[] = "noiseMode";     // 噪声配色模式。
inline constexpr char kOriginX[] = "originX";         // grid 基准点（SPEC §3.4.3）。
inline constexpr char kOriginY[] = "originY";         // 同上。
inline constexpr char kRemainder[] = "remainder";     // grid 余量策略（discard/keep-partial/pad）。
inline constexpr char kInCanvasAnchor[] = "inCanvasAnchor"; // transform：层在画布内的锚点。
inline constexpr char kLayerPivot[] = "layerPivot";   // transform：层旋转枢轴。
inline constexpr char kImagePlacement[] = "imagePlacement"; // 图层内层两级落位段（可选）。
inline constexpr char kFit[] = "fit";                 // 源图→图层框适应方式（crop/stretch/native）。
inline constexpr char kBoxWidth[] = "boxWidth";       // 落位框宽。
inline constexpr char kBoxHeight[] = "boxHeight";     // 落位框高。
inline constexpr char kImageAnchor[] = "imageAnchor"; // 源图在框内锚点。
inline constexpr char kImagePivot[] = "imagePivot";   // 源图旋转枢轴。
inline constexpr char kBackdrop[] = "backdrop";       // 落位框背板图案（复用 patternParams 键集）。
} // namespace layer

// ---------------------------------------------------------------------------
// anno：annotations 段逐条元素（src/annotation/annotation_json.cpp；
//       x/y/type/points/closed/cornerRadius/zOrder/transform 复用 common）。
// ---------------------------------------------------------------------------
namespace anno {
inline constexpr char kText[] = "text";               // 文字内容。
inline constexpr char kFontSize[] = "fontSize";       // 文字字号（像素，随变换缩放）。
inline constexpr char kSegments[] = "segments";       // path 段数组。
inline constexpr char kSegType[] = "t";               // path 段的单字母类型键（值为 M/L/Q/C/Z）。
inline constexpr char kCoords[] = "coords";           // path 段的坐标数组。
inline constexpr char kColor[] = "color";             // 描边色（十六进制串）。
inline constexpr char kFillColor[] = "fillColor";     // 填充色。
inline constexpr char kStrokeWidth[] = "strokeWidth"; // 线宽。
inline constexpr char kMatrix[] = "matrix";           // transform.matrix（6 元仿射）。
inline constexpr char kScaleX[] = "scaleX";           // transform.scaleX（OBB 缩放）。
inline constexpr char kScaleY[] = "scaleY";           // transform.scaleY。
inline constexpr char kRotationDeg[] = "rotationDeg"; // transform.rotationDeg。
} // namespace anno

// ---------------------------------------------------------------------------
// pre：预处理步骤 {op, ...params}（写出在 engine_config_json_internal.h，读取在 engine_config_json.cpp）。
// ---------------------------------------------------------------------------
namespace pre {
inline constexpr char kOp[] = "op";                   // 步类型（gray/split/invert/rotate/flip/resize）。
inline constexpr char kMask[] = "mask";               // split/invert 的通道掩码（"111" 之类）。
inline constexpr char kDeg[] = "deg";                 // rotate 角度。
inline constexpr char kAxis[] = "axis";               // flip 轴向（horizontal/vertical）。
inline constexpr char kMul[] = "mul";                 // **旧配置迁移专用**：已废弃 opacity 步的乘算量。
inline constexpr char kAdd[] = "add";                 // **旧配置迁移专用**：已废弃 opacity 步的加算量。
} // namespace pre

} // namespace idc::core::texts::jsonkey

// ===========================================================================
// defaults：不写入配置文件就看不到、写入前又有固定初值的「数据值」——相当于 §7 的缺省值总表。
//   收拢在此的理由：这些值同时用于结构体成员初值与 §7 读取端回退两处，两处必须同值才能
//   保证「没写该键」与「写默认值」得到同一张图；分开写则改动时极易漏改一处。
//   可译性：**不可译**（是要落盘的数据，与枚举 token 表同源，翻译即破协议）。
// ===========================================================================
namespace idc::core::texts::defaults {

// 预处理通道掩码（'1'＝参与、'0'＝退出，逐位对应 R/G/B）。
inline constexpr char kMaskAllOn[] = "111";   // 全通道：SplitOp/InvertOp 成员默认值、§7 读取回退，
                                              // 也是 pixel_kernels / preprocess_pipeline 里「融合前状态」的累加初值与哨兵。
inline constexpr char kMaskAllOff[] = "000";  // 无通道：融合累加器初值的另一端；灰度图上代表「空操作」。

// 导出命名模板（SPEC §4.1）。
inline constexpr char kNamingTemplate[] = "{name}_{index:03d}";  // 出厂模板：CompositionParams / EmitParams 两处成员初值、
                                                                // §7 读取回退、模板为空时的兜底均引此（GUI 命名框初值不在此列：跨层 char[] 无法转引，另立 gui_texts.h ident::kNamingDefaultTemplate，两处必须同字）。
inline constexpr char kRegionFallbackName[] = "region";         // {name} 取不到目标文件夹名时的回退占位名（分离导出）。

// §7 缺字段时的回退 token（值域与各枚举的 token 映射表一致，改动须两边同步）。
inline constexpr char kTierErase[] = "erase";                    // 缺 cut.tier：默认反向剔除（Tier::Erase）。
inline constexpr char kGeneratorRect[] = "rect";                 // 缺 cut.generator。
inline constexpr char kPolarityRemove[] = "remove";              // 缺 cut.polarity。
inline constexpr char kStrategyRowMajor[] = "row-major";         // 缺 order.strategy 与 emit.merge.arrange.strategy（两处）。
inline constexpr char kEmitModeMerged[] = "merged";              // 缺 emit.mode。
inline constexpr char kFormatPng[] = "png";                      // 缺 emit.format。
inline constexpr char kLayoutCollapse[] = "collapse";            // 缺 emit.merge.layout。
inline constexpr char kRemainderDiscard[] = "discard";           // 缺 cut.params.grid.remainder。
inline constexpr char kPatternKindSolid[] = "solid";             // 缺 generate.kind / 图层 recipe.kind。
inline constexpr char kPatternBackground[] = "#FFFFFFFF";        // 缺配方 background：白底不透明。
inline constexpr char kPatternForeground[] = "#FF000000";        // 缺配方 foreground：黑前景（与 background 可各自演化，故不合并）。
                                                                  // 与 defaults::kAnnotationColor 同字不同义（图案前景 / 标注色），各立一条。
inline constexpr char kPatternOrientation[] = "horizontal";      // 缺配方 orientation。
inline constexpr char kNoiseModeFullColor[] = "full-color";      // 缺配方 noiseMode。
inline constexpr char kAnchorCenter[] = "center";                // 缺层在画布锚点 / 层枢轴 / 图在框锚点 / 图枢轴（共 6 处同值回退）。
inline constexpr char kImageFitCrop[] = "crop";                  // 缺 fit：源图→图层框的适应方式（图层与底图两处）。
inline constexpr char kFlipAxisHorizontal[] = "horizontal";      // 缺 preprocess 步的 axis。与 kPatternOrientation 同字不同义，
                                                                  // 分开定义以免两处日后被误当作同一条修改。
inline constexpr char kPreStepOpacity[] = "opacity";             // **旧配置迁移专用**：已废弃的预处理步名哨兵。
                                                                  // 与 common::kOpacity 同字不同义（现行键名 / 旧步名），各立一条以免被误当同一条修改。
inline constexpr char kLayerSourceEmpty[] = "empty";             // 缺 layers[i].source.kind：降级为空占位层。
inline constexpr char kPadColorTransparent[] = "#00000000";      // 缺 emit.merge.padColor：拼贴余量全透明。
inline constexpr char kAnnotationColor[] = "#FF000000";          // 缺 annotations[i] 的 color 与 fillColor（两处同值）。
                                                                  // 与 defaults::kPatternForeground 同字不同义，各立一条以免日后误当同一条修改。
inline constexpr char kAnnotationText[] = "A";                   // 缺 text 时的占位文字（保证 TEXT 标注不会因空串而不可见）。
                                                                  // GUI 默认文字初值同字另立 gui_texts.h label::kDefaultAnnotationText（跨层不能转引，两处必须同字）。

} // namespace idc::core::texts::defaults

// ===========================================================================
// msg：面向人的错误与校验报因、图案显示名（Core 无日志设施，失败原因经 result.error / err / reason
//   逐层上抛，由 GUI 弹窗与 CLI stderr 原样展示）。本组**可译**，使用点一律走 IDC_KTR / IDC_KTRF；
//   源语言（zh-CN）下目录为空，取回的就是本文件这些中文字面量本身。
//   注意：面向人的引擎报因不携带 E-x / SPEC §x 等内部标记（用户无从对照）；
//   字段名（points/fontSize/type）仍属内部术语，不译；
//   以「'」收尾的片段只存前段，后接的原值与尾引号就地拼接；整句里的运行期值用 %1/%2 占位（见下）。
// ===========================================================================
namespace idc::core::texts::msg {

// 引擎执行失败（src/engine/engine.cpp）。
inline constexpr char kEngineSourceEmpty[] = "源图像为空，无法切割";   // 图像入口空图检查（runEngine）与几何主体尺寸非正检查（runEngineImpl，含仅尺寸入口）两处同文；预处理后为空另见下行专句。
inline constexpr char kEngineRectDegenerate[] = "选框无效：矩形宽高需为正，无法生成切割线";
inline constexpr char kEngineMultiRectEmpty[] = "多矩形为空：没有可用矩形";
inline constexpr char kEngineNoGrid[] = "切割未产生有效网格：单元尺寸或图像尺寸需为正";
inline constexpr char kEngineDeleteSetEmpty[] = "删除区为空：没有单元被选中剔除";
inline constexpr char kEngineKeepSetEmpty[] = "保留集为空：删除区覆盖了全图，或没有选中单元";
inline constexpr char kEngineRearrangeNeedsL3[] = "合并重排仅网格模式支持：请改用合并坍缩或分离导出";
inline constexpr char kEngineNotCollapsible[] = "保留集不可坍缩，且标准提取 / 反向剔除不支持合并重排："
                                                "请调整切割线使删除区覆盖整行/整列，或改用分离导出";
inline constexpr char kEngineSourceEmptyAfterPreprocess[] = "预处理后图像为空，无法切割";

// 标注 JSON 逐条校验报因（src/annotation/annotation_json.cpp）。
inline constexpr char kErrMissingNumField[] = "缺少或非法数值字段 '%1'";   // %1＝键名（IDC_KTRF 填充）。
inline constexpr char kErrAnchorDegenerate[] = "锚点退化：";             // 后接下列各形状尺寸报因。
inline constexpr char kErrRectSize[] = "矩形宽高须大于 0";
inline constexpr char kErrSquareSize[] = "正方形边长须大于 0";
inline constexpr char kErrRhombusSize[] = "菱形两半对角线须大于 0";
inline constexpr char kErrIsosTriangleSize[] = "等腰三角形半底与高须大于 0";
inline constexpr char kErrEquTriangleSize[] = "等边三角形尺寸须大于 0";
inline constexpr char kErrRectTriangleSize[] = "直角三角形两直角边须大于 0";
inline constexpr char kErrEquRectTriangleSize[] = "等腰直角三角形直角边须大于 0";
inline constexpr char kErrCircleDegenerate[] = "锚点退化：圆的半径须大于 0";   // 整句（不与前缀拼接）。
inline constexpr char kErrEllipseDegenerate[] = "锚点退化：椭圆两半轴须大于 0";
inline constexpr char kErrNoPoints[] = "缺少 points 数组";
inline constexpr char kErrLineNeeds2Points[] = "直线 points 须恰为 2 顶点";
inline constexpr char kErrPolylineNeedsPoints[] = "折线 points 须 ≥2 顶点";
inline constexpr char kErrPathNoSegments[] = "path 缺少非空 segments 数组";   // 段数组缺失 / 为空 / 非数组。
inline constexpr char kErrPathNoSegment[] = "path 无有效段";                       // 段存在但逐段校验后无一可施加。
inline constexpr char kErrFontSizeNotPositive[] = "文字 fontSize 须为正";
inline constexpr char kErrFontSizeNotNumber[] = "文字 fontSize 须为数值";
inline constexpr char kErrNoTypeField[] = "缺少 type 字段";
inline constexpr char kErrUnknownType[] = "未知 type：'";      // 后接原值与 '。
inline constexpr char kErrBadColor[] = "color 非法十六进制：'";
inline constexpr char kErrBadFillColor[] = "fillColor 非法十六进制：'";
inline constexpr char kErrStrokeWidthNotInt[] = "strokeWidth 须为整数";
inline constexpr char kErrZOrderNotInt[] = "zOrder 须为整数";
inline constexpr char kErrNotAnObject[] = "非对象条目";              // 整档与单文本两条入口共用。
inline constexpr char kErrAnnoSectionInvalid[] = "配置 annotations 段存在非法条目：";
inline constexpr char kErrAnnoTextEmpty[] = "标注 JSON 为空";
inline constexpr char kErrAnnoRootType[] = "标注 JSON 根须为对象（单条标注）或数组（多条标注）";
inline constexpr char kErrAnnoValidationFailed[] = "标注校验失败：";
inline constexpr char kErrAnnoNoValidEntry[] = "标注 JSON 无有效条目";
inline constexpr char kErrAnnoParseFailed[] = "标注 JSON 解析失败：";  // 后接异常 what()。

// 逐条报因的裹句（整句 + 占位符：%1＝序号、%2＝上一批各条报因）。
// 收为单条而非拼接碎片：拆开后译者无法重组语序（英语把序号放句尾）；
// IDC_KTRF 按**编号**填值，译文可自由调序，zh-CN 下替换结果与直接字符串拼接逐字节相同。
inline constexpr char kErrAnnoItemPrefix[] = "第 %1 个标注元素：%2";      // 配置文件的 annotations 段（逐条回传）。
inline constexpr char kErrAnnoElementPrefix[] = "第 %1 个元素：%2";      // 标注文本入参逐条解析（与上条同构但措辞不同）。

// 图案类型显示名（src/generate/pattern.cpp::patternKindName）：供 GUI「新建画布」图案下拉与
//   错误提示现取。**与落盘 token 无关**——§7 写的是 defaults 的 patternKind 串与 gen*Token 那套小写
//   英文串，本节名字仅供人读，故可译；默认分支复用 kPatternSolid。
inline constexpr char kPatternSolid[] = "纯色";
inline constexpr char kPatternLinearGradient[] = "线性渐变";
inline constexpr char kPatternRadialGradient[] = "径向渐变";
inline constexpr char kPatternCheckerboard[] = "棋盘格";
inline constexpr char kPatternStripes[] = "条纹";
inline constexpr char kPatternGridLines[] = "网格线";
inline constexpr char kPatternDotGrid[] = "点阵";
inline constexpr char kPatternConcentric[] = "同心环";
inline constexpr char kPatternRadialRays[] = "放射线";
inline constexpr char kPatternHueGradient[] = "色相渐变";
inline constexpr char kPatternNoise[] = "噪声";

} // namespace idc::core::texts::msg
