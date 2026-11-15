[ ProxyLayer 一键安装包 ]

【这个包里有什么】
  install.bat                   双击即可一键安装（推荐）
  uninstall.bat                 一键卸载 / 还原
  install.ps1                   实际安装逻辑（可加参数，见下）
  Plugin/ProxyLayer.aux2        主插件
  LuaProxy/luaJIT.dll           可选的 luaJIT 同名转发代理（实验组件）

【怎么用】
  1. 把整个 zip 解压到任意文件夹（不要只解压其中某个文件）。
  2. 双击 install.bat。
  3. 看到 [OK] 提示后，重启 AviUtl2。
  4. 编辑菜单 ->「代理图层」->「整个工程自动代理 (开/关)」打开开关。

【脚本会自动做什么】
  - 自动定位 aviutl2.exe：优先用正在运行的宿主，其次常见安装位置，
    再浅层扫描各磁盘；找不到会提示你手动输入路径。
  - 自动判断数据目录：若 exe 同目录已有 data/ 则是便携模式，
    否则用 %ProgramData%\aviutl2。绝不会新建 exe 同目录的 data/
    （那会让 AviUtl2 误切换到便携模式，配置全部「恢复初始化」）。
  - 把 ProxyLayer.aux2 装到 数据目录\Plugin\，同名旧文件先备份。
  - luaJIT 转发代理（可选，用于把 obj.w/obj.h 映射回原始尺寸）：
      * 先把原来的 luaJIT.dll 备份为 luaJIT.dll.bak-<时间戳>
      * 再把它改名为 luaJIT_real.dll（代理 DLL 的转发目标）
      * 最后把本包的代理 DLL 复制成 luaJIT.dll
    如果宿主目录本来就没有 luaJIT.dll，会跳过这步并给出提示，
    主插件仍然可以正常使用。
  - 幂等：重复运行不会重复改名，也不会重复备份。

【参数】
  powershell -ExecutionPolicy Bypass -File install.ps1 -ExePath "D:\AviUtl2\aviutl2.exe"
      手动指定宿主路径。
  ... -SkipLuaProxy
      只装主插件，完全不碰 luaJIT.dll。
  ... -Uninstall
      还原：把代理 luaJIT.dll 移开，把 luaJIT_real.dll 改回 luaJIT.dll，
      并把数据目录里的 ProxyLayer.aux2 改名保留。脚本不删除任何文件，
      所有被移开的文件都带 .removed-<时间戳> 后缀，可随时手动还原。

【注意】
  - 安装时如果 AviUtl2 正在运行，文件仍可替换，但必须重启宿主才会加载新 DLL。
  - 所有被覆盖的文件都会留下 .bak-<时间戳> 备份，可随时手动还原。
  - 预览画质会下降是设计使然，导出始终使用原图。
  - 首次播放需要构建代理缓存，开头几秒偏慢。

【另一个包】
  同版本的 ProxyLayer-<tag>.au2pkg.zip 是 AviUtl2 拖入式安装包，
  只含主插件，不修改 exe 同目录任何文件；适合不想动 luaJIT.dll 的用户。
