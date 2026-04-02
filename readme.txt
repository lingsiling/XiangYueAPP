1. 服务端接入 SQLite：
   - 数据库文件固定保存到 D:/Qt/Projects/XiangYueAPP/database/xiangyue.db
   - 启动服务器时自动创建目录、打开数据库、初始化表结构
   - 新增/完善 users、resources、uploads、favorites 表及相关索引

2. 按分层思路实现登录/注册：
   - DBManager：负责数据库连接、建表、错误输出
   - UserRepository：封装 users 表的查询/插入
   - AuthService：封装注册/登录业务逻辑（当前阶段不考虑安全，密码明文存入 password_hash 字段）

3. 通信协议扩展（行协议，\n 结尾）：
   - REGISTER##username##password
   - LOGIN##username##password
   - 返回 REGISTER_OK/REGISTER_FAIL、LOGIN_OK/LOGIN_FAIL

4. 客户端 LogDialog 改造：
   - 点击“注册/登录”发送对应命令
   - 增加接收缓冲区按行解析，解决粘包
   - 只有收到 LOGIN_OK 才进入主界面