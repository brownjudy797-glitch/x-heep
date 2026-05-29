# Verilator 5.020 仿真构建问题修复记录

## 环境

- FuseSoC 2.4.6
- Verilator 5.020 (Debian 包)
- 目标：`openhwgroup.org:systems:core-v-mini-mcu`，`sim` target，`verilator` tool

## 问题 1：serial_link include 文件找不到

**错误**：
```
%Error: .../serial_link.sv:11:10: Cannot find include file: axis/typedef.svh
```

**根因**：`hw/vendor/pulp_platform_serial_link.core` 中，include fileset 对**目录**使用了 `is_include_file: true`：

```yaml
# 原来的写法
- pulp_platform/serial_link/src/axis/include/axis : {is_include_file : true}
```

FuseSoC/Edalize 会添加 incdir 路径，但**不会复制目录内的文件**到构建目录。

**修复**：改为逐个列出文件，每个文件标记 `is_include_file: true`：

```yaml
- pulp_platform/serial_link/src/axis/include/axis/typedef.svh : {is_include_file : true}
- pulp_platform/serial_link/src/axis/include/axis/assign.svh : {is_include_file : true}
```

**修改文件**：`hw/vendor/pulp_platform_serial_link.core`

---

## 问题 2：prim_util_memload.svh 找不到

**错误**：
```
%Error: .../prim_generic_ram_1p.sv:67:12: Cannot find include file: prim_util_memload.svh
```

**根因**：`primgen.py` 生成的 `prim_abstract_ram_1p.core` 直接把 generic RTL 文件加入 files 列表（绕过 FuseSoC 2.x 对生成 core 的依赖解析缺陷），但 generic core 的**传递依赖**（`lowrisc:prim:util_memload`）没有被包含。

FuseSoC 2.x 不会解析 generated core 的传递依赖，所以 `prim_util_memload.svh` 没有被复制到构建目录。

**修复**：在 `primgen.py` 的 `_generate_abstract_impl()` 中，对需要 `util_memload` 的 primitives（`ram_1p`, `ram_2p`, `rom`），直接把 `.svh` 文件加入生成 core 的 files 列表：

```python
_MEMLOAD_PRIMS = {'ram_1p', 'ram_2p', 'rom'}
if prim_name in _MEMLOAD_PRIMS:
    memload_src = os.path.join(
        os.path.dirname(__file__), '..', 'rtl', 'prim_util_memload.svh')
    memload_relpath = os.path.relpath(
        os.path.abspath(memload_src),
        os.path.dirname(abstract_prim_core_filepath))
    gen_files.append({memload_relpath: {'is_include_file': True}})
```

**修改文件**：`hw/vendor/lowrisc/opentitan/hw/ip/prim/util/primgen.py`

---

## 问题 3：281 个 Verilator 警告被视为错误

**错误**：
```
%Error: Exiting due to 281 warning(s)
```

**根因**：Verilator 5 默认将所有 warning 当作 fatal error。主要警告来源：
- `yosys_spiflash.sv`（行为级仿真模型，大量阻塞赋值）
- AXI 模块（DECLFILENAME：文件名与模块名不匹配）
- 各类仿真代码的 UNUSEDSIGNAL、WIDTHTRUNC 等

**修复**：两步处理：

1. 在 `core-v-mini-mcu.core` 的 `verilator_options` 中添加 `-Wno-fatal`，让警告不致命
2. 在 `tb/tb_v5.vlt` 中添加全局 waiver 规则，抑制常见的仿真代码警告

**修改文件**：`core-v-mini-mcu.core`、`tb/tb_v5.vlt`

---

## 问题 4：Verilator 5.020 chandle 类型 trace 生成 bug

**错误**：
```cpp
Vtestharness__Trace__0__Slow.cpp:8483:123: error: expected unqualified-id before ',' token
tracep->declQuad(..., VerilatedTraceSigType::, false,-1, 63,0);
```

**根因**：`uartdpi.sv` 中声明了 `chandle ctx`——DPI 不透明指针类型。Verilator 5.020 在生成 trace 代码时，无法将 `chandle` 映射到 `VerilatedTraceSigType` 枚举值，生成了空的 `VerilatedTraceSigType::`。

**修复（临时）**：构建后修复生成的 C++ 文件：
```bash
sed -i 's/VerilatedTraceSigType::,/VerilatedTraceSigType::LOGIC,/g' \
    build/.../sim-verilator/Vtestharness__Trace__0__Slow.cpp
```

**永久方案**：使用 `build_sim.sh` 包装脚本自动完成上述修复步骤。

这是一项 Verilator 5.020 的已知缺陷，未来升级 Verilator 版本后可以移除此 workaround。

**关联文件**：`hw/vendor/lowrisc/opentitan/hw/dv/dpi/uartdpi/uartdpi.sv`（问题源）、`build_sim.sh`（包装脚本）

---

## 问题 5：链接时缺少 libelf

**错误**：
```
/usr/bin/ld: cannot find -lelf: No such file or directory
```

**修复**：安装开发包：
```bash
sudo apt-get install -y libelf-dev
```

---

## 仿真运行方法

```bash
cd build/openhwgroup.org_systems_core-v-mini-mcu_1.0.5/sim-verilator
./Vtestharness +firmware=/home/rime/x-heep/sw/build/main.hex
```

波形文件 `waveform.fst` 会在运行后生成，可用 GTKWave 查看：
```bash
gtkwave waveform.fst
```

## 修改文件清单

| 文件 | 改动内容 |
|---|---|
| `hw/vendor/pulp_platform_serial_link.core` | include 文件从目录改为逐个文件 |
| `hw/vendor/lowrisc/opentitan/hw/ip/prim/util/primgen.py` | 添加 util_memload 到生成 core；通用 files 列表重构 |
| `core-v-mini-mcu.core` | Verilator 选项添加 `-Wno-fatal` |
| `tb/tb_v5.vlt` | 添加全局 warning waiver 规则 |
| `build_sim.sh` | **新建**：自动化构建包装脚本 |

## 前置 session 修复（简要）

- 移除 35+ `.core` 文件中的空 waiver fileset
- 修复 `fpv/` 目录下自引用 YAML 锚点（添加 `FUSESOC_IGNORE`）
- `primgen.py`：添加 `_scan_cores()` 兼容 FuseSoC 2.x（无 `cores` GAPI key）、修复 3 段 VLNV 解析
- 更新 Verilator 5 waiver 规则（`cve2_v5.vlt`、`register_interface_v5.vlt`）
- 修复 `waiver-gen.py` 的 Debian Verilator 版本解析
