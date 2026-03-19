# SXL2 智能楼梯爬行监测系统（课程软件设计阶段）

## 1. 项目简介
本项目面向“泛在物联网课程”软件设计阶段，基于给定九轴传感器数据文件（`data1.txt`、`data2.txt`）实现一套端到端流程：

- 模拟数据采集终端（按采样频率发送）
- 后台服务端接收、处理、存储
- Web 前端实时展示与历史查询
- 预留后续硬件接入（STM32/真实传感器）接口

## 2. 你问的两个问题（直接结论）
### 2.1 模拟终端是否已实现？
已实现，当前为**文件驱动的模拟终端**，核心能力如下：

- 多文件顺序发送（默认 `data1.txt -> data2.txt`）
- 循环发送（可配置）
- 发送计数（`file_lines`）
- 启停控制（`/start`、`/stop`、`/reset`）
- 服务端侧算法处理（步态检测、楼层累计、99 层上限）

对应代码：
- `simulator.c` / `simulator.h`
- `server.c`（加载数据源与线程编排）

### 2.2 用的什么语言？
- 后端与模拟终端：**C（C99）**
- 前端页面：**HTML + CSS + JavaScript**
- 数据库：**SQLite（本地文件库）**
- 构建工具：**CMake**

## 3. 系统架构
### 3.1 模块划分
1) 采集模拟层
- 从 `data1.txt`、`data2.txt` 读取九轴样本
- 按采样周期推送到处理链（当前在同进程模拟）

2) 服务处理层
- 预处理与简单步态事件识别
- 计算步数、楼层、速度
- 控制逻辑：启动/停止/重置

3) 存储层
- SQLite 持久化
- 保存历史记录与分析结果

4) 展示层
- `index.html` 实时显示当前楼层、步数、速度、发送行数
- 历史记录表格展示

### 3.2 关键文件说明
- `server.c`：进程入口、初始化、线程启动
- `simulator.c`：模拟终端（多文件、循环、计数、频率）
- `web_server.c`：HTTP 路由与接口
- `db_manager.c`：建表与读写
- `globals.c` / `global_vars.h`：全局状态与共享数据
- `audio_hint.c`：提示音触发逻辑（当前为日志提示）
- `pthread_compat.h`：Windows 下线程兼容层

## 4. 已实现功能（对照基础要求）
### 4.1 模拟数据采集终端
- [x] 多文件顺序发送
- [x] 循环发送
- [x] 发送计数
- [x] 启停控制
- [x] 预留硬件适配方向（当前为文件源）

### 4.2 数据处理与算法
- [x] 基础干扰抑制（平滑 + 阈值）
- [x] 楼层识别与累计
- [x] 最大楼层 99 限制
- [x] 核心识别逻辑在服务端执行

### 4.3 后台服务与数据库
- [x] HTTP 服务
- [x] 并发请求处理（多线程）
- [x] SQLite 持久化
- [x] 核心表：`users`、`devices`、`raw_samples`、`analysis_results`、`history_records`

### 4.4 前端可视化
- [x] 实时状态显示
- [x] 历史查询
- [x] 启停与重置控制
- [ ] 波形绘制（当前为数值与列表，后续可接入图表库）
- [ ] 角色化页面（admin/user/monitor 细分 UI 待补）

## 5. API 说明（当前版本）
- `GET /`：返回 Web 页面
- `POST /api/login`：登录，参数：`username`、`password`
- `GET /api/session`：获取当前登录会话信息
- `GET /api/collectors`：获取采集对象列表（支持多个用户）
- `GET /api/status`：实时状态
  - 返回字段：`username`、`device_id`、`current_floor`、`climbed_floors`、`steps`、`speed`、`file_lines`、`running`、`max_floors`、`profile`
  - 波形字段：`accel_wave`（加速度波形）、`gyro_wave`（角速度波形）
- `GET /api/history`：最近历史记录
- `POST /api/sim/load`：加载模拟场景，参数：`profile=mixed|upstairs3`
- `POST /start`：启动模拟
- `POST /stop`：停止模拟
- `POST /reset`：重置状态并重新开始采样

## 5.1 默认登录账号
- 管理员：admin / admin123
- 监控人员：monitor1 / monitor123
- 采集对象：collector1 / collector123（另含 collector2）

角色说明：
- admin：全权限
- monitor：可加载场景、开始/停止/重置、监控多采集对象
- user：可查看监测数据与历史记录

## 6. 构建与运行
在项目上层目录执行（当前工程在 `SXL2/`）：

```powershell
cmake -S .\SXL2 -B .\SXL2\build
cmake --build .\SXL2\build --config Release -j 4
.\SXL2\build\Release\server.exe
```

启动后访问：
- `http://127.0.0.1:8080/`

## 7. 数据说明
- `data1.txt`：上楼场景数据（爬楼）
- `data2.txt`：日常行走数据（对照）

文件格式为六列（示例头）：
- `gx gy gz ax ay az`

## 8. 已知限制与后续计划
### 8.1 当前限制
- 角色权限（admin/user/monitor）已完成数据层预留，完整鉴权与页面隔离待继续完善
- 原始波形图与回放未做图表化展示
- 采集终端与服务端目前同进程模拟，尚未拆成独立进程/独立设备

### 8.2 下一步建议
1. 增加登录与 token 鉴权、角色权限矩阵
2. 加入图表库实现实时波形和历史回放
3. 拆分“模拟终端进程”和“服务端进程”，支持多终端独立上报
4. 增加 STM32/串口/Socket 适配器，替换文件源

## 9. 验收演示建议流程
1. 启动服务
2. 页面点击“开始模拟”
3. 观察实时楼层、步数、速度、发送行数变化
4. 查看历史记录是否持续写入
5. 点击“停止”与“重置”验证控制有效

---
如需课程提交版文档（含架构图、ER 图、接口时序图、分工说明），可在此 README 基础上扩展成《软件设计说明书》版本。
