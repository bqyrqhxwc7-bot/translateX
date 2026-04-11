# translateX

translateX 是一个基于 Qt 的桌面翻译工具，支持本地 Ollama 翻译、云端翻译、网络大模型 API 以及高级翻译提示和上下文翻译配置。

## 主要功能

- 本地 Ollama 翻译后端支持
- 云端线上翻译回退
- 自定义普通翻译提示词和上下文翻译提示词
- DeepSeek v3.2 预设支持
- 网络大模型 API 地址与 API Key 配置
- 严格输出模式，避免模型输出解释文本
- 默认配置保存与恢复

## 构建要求

- Qt 6
- CMake 3.21+
- Microsoft Visual Studio（Windows）

## 构建步骤

```powershell
cmake -S . -B build-vs2026-x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-vs2026-x64 --config Release
```

## NSIS 安装程序打包

当前项目支持 NSIS 安装包打包。要生成安装程序，需要先安装 NSIS 并确保 `makensis` 在系统 `PATH` 中。

推荐安装方法：

- Windows: 从 https://nsis.sourceforge.io/ 下载并安装
- 或者使用 `choco install nsis`（如果已安装 Chocolatey）

然后在构建目录中运行：

```powershell
cmake --build build-vs2026-x64 --config Release --target package
```

如果环境中未安装 NSIS，项目会回退生成 ZIP 包。

## 敏感信息说明

打包产物仅包含应用可执行文件和运行时依赖，不会包含用户本地配置中的 API Key 或其他敏感设置。应用设置保存在用户机器上的本地配置区。

## 许可证

本项目采用 MIT 许可证，详见 `LICENSE` 文件。

## 许可证

本项目采用 MIT 许可证，详见 `LICENSE` 文件。
>>>>>>> 95f43f9 (Initial translateX project commit)
