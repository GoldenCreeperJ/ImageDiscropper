# src/io

**目录作用**：`include/io` 接口的实现——stb_image 解码、stb_image_write 编码（PNG/JPEG/BMP）、libwebp 编码（WebP）。

**分块依据**：与 `include/io` 头文件一一对应。本目录是全工程 **唯一定义 `STB_IMAGE_IMPLEMENTATION` /
`STB_IMAGE_WRITE_IMPLEMENTATION` 的翻译单元**（header-only 库要求），其余模块经 `image_io.h` 间接调用，
不直接接触 stb / libwebp，避免重复符号与依赖扩散。

| 文件             | 职责                                                                                                                                     |
|----------------|----------------------------------------------------------------------------------------------------------------------------------------|
| `image_io.cpp` | 解码（→ RGBA）与编码（PNG/JPEG/BMP/WebP）。`readImageFile` 经 `pathFromUtf8` 读进内存后解码；`writeImageFile` 先编码到内存再落盘——绕开 stb 内部窄字符 `fopen`，支持中文 / 非 ASCII 路径。 |
