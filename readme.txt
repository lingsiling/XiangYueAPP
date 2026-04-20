# XiangYueAPP / XiangYueServer（学习资源共享平台）

一个基于 **Qt Widgets + TCP** 的简单“资源共享平台”示例项目，包含：
- **客户端（XiangYueAPP）**：登录/注册、资源列表、搜索、上传（支持多文件）、下载、资源详情（评论区、下载）
- **服务端（XiangYueServer）**：多线程接入、文件保存、SQLite（用户/评论等）

> 协议为“**文本行协议 + 二进制数据流**”混合模式：控制指令以 `\n` 结尾，文件内容按 size 精确收发。

---

## 目录结构（核心文件）

### Client
- `logdialog.*`：登录/注册界面；登录成功后将同一个 `QTcpSocket*` 交给主界面使用（避免重复连接）
- `usersession.*`：登录后的“用户会话信息”（`userId/username/avatar`），用于解耦 UI 模块
- `mainwindow.*`：主界面（用户名、头像、资源列表、搜索、上传、打开资源详情、日志输出）
- `resourcedetaildialog.*`：资源详情对话框（下载、评论区显示与发送、评论删除）
- `fileclient.*`：客户端网络逻辑（LIST/UPLOAD/DOWNLOAD/FILE/评论等协议解析；**多文件上传队列**）
- `resourcesearch.*`：资源搜索逻辑（与 UI 分离）

### Server
- `threadedtcpserver.*`：继承 `QTcpServer`，在 `incomingConnection()` 中拿到 `socketDescriptor`
- `clientworker.*`：每个连接对应一个 worker + 独立线程；内部创建 `QTcpSocket` 并接管 descriptor
- `dbmanager.*`：SQLite 初始化、建表/索引
- `authservice.* / userrepository.*`：注册/登录业务层与数据访问层
- `commentservice.* / commentrepository.* / commentrecord.h`：评论业务层与数据访问层（低耦合）

---

## 通信协议（简化说明）

所有**控制消息**一律以 `\n` 结尾（便于按行拆包）。

### 客户端 -> 服务端
- 请求资源列表：
  - `LIST\n`
- 下载资源文件：
  - `DOWNLOAD##fileName\n`
- 上传资源文件（单文件协议；多文件上传是“重复多次”发送该协议）：
  - 先发头：`UPLOAD##fileName##fileSize\n`
  - 再紧跟发送 `fileSize` 字节二进制内容
- 注册：
  - `REGISTER##username##password\n`
- 登录：
  - `LOGIN##username##password\n`
- 获取头   （复用 FILE 传输）：
  - `GET_AVATAR##userId\n`

#### 评论（content 支持换行）
由于评论内容允许换行和特殊字符，协议中 `content` 统一用 **Base64** 表示（避免破坏按行解析/分隔符解析）：

- 拉取某资源的评论列表：
  - `COMMENT_LIST##resourceName\n`
- 发送评论：
  - `COMMENT_ADD##userId##resourceName##content_b64\n`
- 删除评论（服务端强校验：只能删自己的）：
  - `COMMENT_DEL##userId##commentId\n`

> `content_b64 = base64(UTF-8(content))`

---

### 服务端 -> 客户端
- 资源列表：
  - `LIST##f1##f2##f3...\n`
- 下载响应（资源文件 / 头像文件都走该协议）：
  - 先发头：`FILE##fileName##fileSize\n`
  - 再紧跟发送 `fileSize` 字节二进制内容
- 上传完成确认：
  - `UPLOAD_OK##fileName\n`
- 登录成功：
  - `LOGIN_OK##userId##username##avatar\n`
- 登录失败：
  - `LOGIN_FAIL##reason\n`

#### 评论列表返回（分批多行）
- `COMMENT_BEGIN##resourceName\n`
- `COMMENT_ITEM##commentId##userId##username_b64##createdAt_b64##content_b64\n`
- `COMMENT_END##resourceName\n`

发送评论结果：
- `COMMENT_ADD_OK##commentId\n`
- `COMMENT_ADD_FAIL##reason\n`

删除评论结果：
- `COMMENT_DEL_OK##commentId\n`
- `COMMENT_DEL_FAIL##reason\n`

---

## 多文件上传流程说明（客户端）

目标：**一次选择多个文件，按队列顺序逐个上传**；并且只有在服务端返回 `UPLOAD_OK` 后才上传下一个，保证状态一致。

### 关键设计（低耦合）
- UI（`MainWindow`）只负责：
  - 通过 `QFileDialog::getOpenFileNames()` 选择多个文件路径
  - 调用 `FileClient::uploadFiles(paths)`
  - 监听 `FileClient::logLine`，把日志追加到 `textEdit`
- `FileClient` 负责：
  - 维护上传队列 `m_uploadQueue`
  - 一次只发一个文件：发 `UPLOAD##...` + 二进制
  - 收到 `UPLOAD_OK##fileName` 后，关闭当前文件句柄并 `startNextUpload()`

### 流程
1. 用户点击“上传文件”，多选文件。
2. `MainWindow` 调用 `fileClient->uploadFiles(paths)`。
3. `FileClient` 将每个路径转换为 `UploadTask` 入队，并启动 `startNextUpload()`：
   - 发送头 `UPLOAD##name##size\n`
   - 发送二进制内容
4. 服务端收满 `fileSize` 字节后回 `UPLOAD_OK##fileName\n`。
5. 客户端收到 `UPLOAD_OK`：
   - emit `logLine("上传完成：xxx")`
   - 关闭当前上传文件句柄
   - 启动下一个任务  若队列为空则结束）
   - 刷新列表 `LIST`

---

## 客户端日志输出（UI textEdit 追加）

- `FileClient` 提供统一信号：
  - `logLine(const QString &line)`
- `MainWindow` 负责展示：
  - `connect(fileClient, &FileClient::logLine, this, [=](const QString &s){ ui->textEdit->append(s); });`

这样可以保证：
- 网络层不依赖 UI 控件（低耦合）
- UI 不关心协议细节，只关心“显示文本”

---

## 头像流程说明（客户端显示头像）

- 服务端 `users.avatar` 存储头像相对路径字符串（例如 `avatars/default.png`）
- 客户端登录成功后进入主界面：
  1. `LogDialog` 解析 `LOGIN_OK##userId##username##avatar`
  2. 把 `UserSession` 注入 `MainWindow`
  3. `MainWindow` 发送 `GET_AVATAR##userId\n`
  4. 服务端读取头像文件并以 `FILE##...` 返回
  5. `FileClient` 复用已有下载逻辑保存到 `ClientSave/` 并发出 `fileReceived(...)`
  6. `MainWindow` 接收 `fileReceived(...)` 并用 `QPixmap` 更新头像 `QLabel`

> 为避免干扰用户，头   文件名以 `avatar_` 前缀回传时，客户端不会弹出“下载完成”提示。

---

## 评论功能流程说明（支持换行）

1. 用户打开资源详情页：
   - `ResourceDetailDialog` 调用 `FileClient::requestComments(resourceName)`
   - 客户端发 `COMMENT_LIST##resourceName\n`
2. 服务端查询 SQLite `comments` 表并返回：
   - `COMMENT_BEGIN` + 多条 `COMMENT_ITEM` + `COMMENT_END`
   - `username/createdAt/content` 都是 base64 字段
3. 客户端 `FileClient` 收齐后 emit：
   - `commentsUpdated(resourceName, comments)`
4. 详情页刷新评论列表，显示格式：
   - `用户名  时间`
   - `内容（可多行）`

发送评论：
- `ResourceDetailDialog` 读取 `textEditComment` 的多行文本
- `FileClient` 将内容 base64 编码后发送 `COMMENT_ADD`
- 成功后自动重新拉取评论列表刷新显示

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

## 数据库（SQLite）

数据库由服务端启动时初始化（建表/索引）：
- `users`：用户信息（username/password_hash/salt/avatar/created_at）
- `comments`：评论（resource_name/user_id/content/created_at）
- 以及预留的 `resources/uploads/favorites` 表（用于后续扩展）

数据库路径示例：
- `D:/Qt/Projects/XiangYueAPP/database/xiangyue.db`

---

## 运行方式

### 1. 启动服务端
- 打开 `XiangYueServer.pro`
- 运行后监听端口：`7777`
- 文件保存目录（示例）：`D:/Qt/Projects/XiangYueAPP/ServerSave/`
- 头像目录（示例）：`D:/Qt/Projects/XiangYueAPP/ServerAvatars/`
  - 需要准备默认头像文件：`default.png`

### 2. 启动客户端
- 打开 `XiangYueAPP.pro`
- 默认连接：`127.0.0.1:7777`
- 登录成功后进入主界面，自动请求资源列表、请求头像

---

## 已知注意事项（重要）

1. **上传完成确认必须在服务端“收满 fileSize 后”再发送**
   - 否则客户端会收到多次 `UPLOAD_OK`，出现“后续上传异常”等问题。
2. **同一个 socket 的 readyRead 只能有一个模块消费**
   - 登录界面登录成功后必须断开自己的 `readyRead`，避免和主界面抢包。
3. **评论内容   用 Base64**
   - 为了支持换行与任意字符，并保持“按行协议”解析稳定。

---

## License
仅用于学习与演示。