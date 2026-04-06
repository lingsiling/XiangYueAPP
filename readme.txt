# XiangYueAPP / XiangYueServer（学习资源共享平台）

一个基于 Qt Widgets + TCP 的简单“资源共享平台”示例项目，包含：
- **客户端（XiangYueAPP）**：登录/注册、资源列表、上传、下载
- **服务端（XiangYueServer）**：多线程接入、文件保存、SQLite 用户表（注册/登录）

> 协议为“文本行协议 + 二进制数据流”混合模式：控制指令以 `\n` 结尾，文件内容按 size 精确收发。

---

## 目录结构（核心文件）

### Client
- `logdialog.*`：登录/注册界面；登录成功后把同一个 `QTcpSocket*` 交给主界面使用
- `mainwindow.*`：主界面，显示资源列表、触发上传、打开资源详情对话框
- `fileclient.*`：客户端网络逻辑（LIST/UPLOAD/DOWNLOAD/FILE 接收解析）

### Server
- `threadedtcpserver.*`：继承 `QTcpServer`，在 `incomingConnection()` 中拿到 `socketDescriptor`
- `clientworker.*`：每个连接对应一个 worker + 独立线程；内部创建 `QTcpSocket` 并接管 descriptor
- `dbmanager.*`：SQLite 初始化、建表
- `authservice.* / userrepository.*`：注册/登录业务层与数据访问层

---

## 通信协议（简化说明）

所有**控制消息**一律以 `\n` 结尾（便于按行拆包）：

### 客户端 -> 服务端
- 请求列表：
  - `LIST\n`
- 下载：
  - `DOWNLOAD##fileName\n`
- 上传：
  - 先发头：`UPLOAD##fileName##fileSize\n`
  - 再紧跟发送 `fileSize` 字节二进制内容

### 服务端 -> 客户端
- 资源列表：
  - `LIST##f1##f2##f3...\n`
- 下载响应：
  - 先发头：`FILE##fileName##fileSize\n`
  - 再紧跟发送 `fileSize` 字节二进制内容
- 上传完成确认：
  - `UPLOAD_OK##fileName\n`

---

## 多线程模型（服务端）

- 主线程仅负责 `listen()` + `incomingConnection()`
- 每个连接创建一个 `QThread`
- `ClientWorker` **moveToThread** 后，在子线程中创建 `QTcpSocket`，并调用 `setSocketDescriptor()`
- 每个 worker 内维护独立的：
  - 接收缓冲区 `m_buf`
  - 上传状态（是否正在收二进制、已收大小、目标文件句柄）

这样可以避免：
- 跨线程使用同一个 `QTcpSocket` 引擎导致的崩溃/未定义行为
- 多连接并发时状态串线

---

## 运行方式

### 1. 启动服务端
- 打开 `XiangYueServer.pro`
- 运行后监听端口：`7777`
- 文件保存目录（示例）：`D:/Qt/Projects/XiangYueAPP/ServerSave/`
- SQLite 数据库（示例）：`D:/Qt/Projects/XiangYueAPP/database/xiangyue.db`

### 2. 启动客户端
- 打开 `XiangYueAPP.pro`
- 默认连接：`127.0.0.1:7777`
- 登录成功后进入主界面，自动请求资源列表

---

## 已知注意事项（重要）

1. **上传完成确认必须在服务端“收满 fileSize 后”再发送**
   - 否则客户端会收到多次 `UPLOAD_OK`，出现“弹窗多次/后续上传异常”等问题。

2. **同一个 socket 的 readyRead 只能有一个模块消费**
   - 登录界面登录成功后必须断开自己的 `readyRead`，避免和主界面抢包。

---

## License
仅用于学习与演示。