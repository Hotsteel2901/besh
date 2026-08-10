# Besh

**besh** 是一个用 C 从零写成的 bash 兼容 shell 同人作品 —— 致敬 bash，
同时融入了 **fish** 与 **zsh** 的现代特性。

## 构建

```sh
make            # 编译出 ./besh
make test       # 运行内置测试套件
make run        # 以交互模式启动
```

依赖：GCC（或任意 C99 编译器）、Linux/POSIX 环境。零第三方库依赖。

## 特性

### bash 兼容核心
- 管道、重定向（`>`, `>>`, `<`, `2>`, `&>`, `<<`, `<<-`, fd 复制）、`&&`/`||`/`;`/`&`
- 变量展开 `$VAR` / `${VAR:-def}` / `${VAR:=def}` / `${VAR:+x}` / `${VAR:?err}`
- 命令替换 `` `cmd` `` 与 `$(cmd)`、算术展开 `$((...))`
- 通配符 `* ? []`、波浪号 `~` / `~user`
- `if / elif / else`, `for`, `while / until`, `case`, `function` 与 `name()`
- 函数与位置参数（`$1..$9`, `$#`, `$@`, `$*`）、`shift`
- 别名 `alias` / `unalias`、环境变量 `export` / `unset`
- 作业控制：`jobs` / `fg` / `bg`、后台 `&`、`wait`

### fish 特性
- **自动建议（autosuggestions）**：键入时以灰色显示历史中最新的匹配命令，
  按 `→` / `Ctrl-F` / `Tab` / `End` 接受
- **语法高亮**：命令（有效=绿、无效=红）、选项（黄）、目录（蓝）、
  变量（青）、数字（品红）、字符串（绿）
- **`abbr` 缩写**：`abbr -a gp "git push"`，输入 `gp` 后按空格或回车自动展开
- **↑ 前缀搜索**：先输入命令前缀再按 `↑`，只回找以此开头的历史命令

### zsh 特性
- **`setopt` / `unsetopt`**（也支持 `set -o` / `set +o`）：
  `autocd`, `globstar`, `noclobber`, `allexport`, `xtrace`,
  `verbose`, `noglob`, `histignoredups`, `autosuggest`, `syntaxhighlight`
- **`autocd` 隐式 cd**：直接输入目录名即可进入（默认开启）
- **`globstar` 递归通配**：`**/*.c` 匹配所有层级的 `.c` 文件（默认开启）
- **花括号展开**：`{a,b,c}`、`{1..5}`、`{a..d}`、嵌套 `{a,b}{1,2}`
- **目录栈**：`pushd` / `popd` / `dirs`、`cd +N` / `cd -N`
- **Ctrl-R 增量反向搜索**（`(reverse-i-search)`），`Ctrl-G`/`ESC` 取消
- **zsh 风格提示符**（`PROMPT`/`PS1` 变量）：
  - `%n` 用户、`%m` 短主机名、`%M` 完整主机名
  - `%~` 家目录缩写路径、`%d` 完整路径、`%#` 特权符
  - `%?` 上次退出码（非零时红色显示）、`%g` git 分支、`%h` 历史行号
  - bash 风格 `\u \h \W \w \$ \t \n \e` 同样可用
- 默认提示符在 git 仓库内会显示当前分支，命令失败后显示红色退出码

### 行编辑器
- Emacs 风格按键：`Ctrl-A/E/B/F`、`Ctrl-K/U/W`、`Ctrl-L`、`Ctrl-D`
- 方向键、Home/End/Delete、Tab 文件名补全（支持多匹配公共前缀 + 列表）
- 历史保存到 `~/.besh_history`，UTF-8 中文输入/粘贴可用
- **启动配置** `~/.beshrc`：每次交互式启动时执行，可持久化
  `alias` / `abbr` / `setopt` / `PROMPT` 等设置：

```sh
# ~/.beshrc 示例
alias ll="ls -l"
abbr -a gp "git push"
setopt autocd globstar
PROMPT='%n@%m %~%g%# '
```

## 用法示例

```sh
# fish 风格
besh> abbr -a gp "git push"
besh> gp<回车>              # 自动展开为 git push
besh> ec<→或Tab>            # 接受灰色建议 echo ...

# zsh 风格
besh> pushd /var/log && dirs -v
besh> echo **/*.sh          # 递归通配
besh> echo {1..10}          # 范围展开
besh> cd -1                 # 回到目录栈

# 选项
besh> setopt autocd globstar
besh> unsetopt autocd       # 关闭隐式 cd
```

## 项目结构

| 文件 | 职责 |
|------|------|
| `lexer.c`   | 词法分析：分词、引号、heredoc |
| `parser.c`  | 递归下降语法分析，生成 AST |
| `executor.c`| AST 执行：管道、重定向、作业控制、展开集成 |
| `expand.c`  | 变量/波浪号/花括号/通配/命令替换展开 |
| `builtins.c`| 全部内建命令（cd、abbr、pushd、setopt、read …） |
| `main.c`    | REPL、行编辑器（高亮/建议/搜索）、提示符、信号 |
| `shell.h`   | 公共头文件与全局状态 |

`web/` 目录是一个展示 besh 的 Next.js 单页落地页（终端模拟、语法高亮演示、
自动建议演示、迷你游戏等）。

```sh
cd web
npm install && npm run dev   # http://localhost:3000
npm run build                # 静态导出到 web/out（GitHub Pages 部署）
```

## 已知限制（与 bash/zsh 的差距）

- **多行编辑**：交互模式下复合命令需写成单行（或写进脚本再 `source`）；行尾 `\` 续行暂不支持
- **不支持的语法**：数组、`local`/`declare`、`[[ ]]`、`${var#pat}` 字符串操作、
  `set -e`/`errexit` 语义、进程替换 `<(...)`、`function` 的 `local`
- **行编辑器按字节处理**：UTF-8 中文可以输入/粘贴，但退格在光标处于多字节字符中间时会删掉一个字节
- **历史文件**：纯文本 `~/.besh_history`，无时间戳、无 fc 编辑
- 未实现 `fc`、`compgen`、`command_not_found_handler`、补全自定义脚本

这些都是"能用"与"好用"之间的差距，日常轻量使用足够，
重型脚本请继续使用 bash/zsh。
