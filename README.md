# Besh

**besh** 是一个用 C 从零写成的 bash 兼容 shell 同人作品 —— 致敬 bash，
同时融入了 **fish** 与 **zsh** 的现代特性。

## 构建

```sh
make                    # 编译出 ./besh（-Wall -Wextra，零警告）
make test               # 差分套件 + 交互式套件 + 展示用例
make test-diff          # 仅差分套件（bash vs besh）
make test-interactive   # 仅交互式套件（行编辑器 / 历史 / 补全）
make run                # 以交互模式启动
make clean
```

依赖：GCC（或任意 C99 编译器）、Linux/POSIX 环境。零第三方库依赖。
交互式测试需要 `python3`（仅测试用，不参与构建）。

## 特性

### bash 兼容核心
- 管道、重定向（`>`, `>>`, `<`, `2>`, `&>`, `>|`, `<<`, `<<-`, `<<<`, fd 复制 `>&N`/`N>&`/`>&-`）
- **复合命令重定向**：`{ ...; } > f`、`if ...; fi 2> err`、`while ...; done < in`
- **花括号命令组** `{ cmd; cmd; }`（在当前 shell 中执行，因此可改变状态）
- 变量展开 `$VAR` / `${VAR:-def}` / `${VAR:=def}` / `${VAR:+x}` / `${VAR:?err}`
- 字符串操作 `${var#pat}` `##` `%` `%%` `/` `//` `/p/r`，以及 `${#var}`、数组下标
- 特殊参数 `$?` `$$` `$#` `$!` `$*` `$@` `$-` `$0..$9`（引号内外一致）
- 命令替换 `` `cmd` `` 与 `$(cmd)`、算术展开 `$((...))`
- **进程替换** `<(...)` / `>(...)`，例如 `diff <(sort a) <(sort b)`
- 通配符 `* ? []`、波浪号 `~` / `~user`
- `if / elif / else`, `for`, `while / until`, `case`, `{ ...; }`, `function` 与 `name()`
- 函数与位置参数、`shift`、`return`、`local` / `declare` / `typeset`（含 `declare -a` 数组）
- 数组：`a=(x y z)`、`${a[0]}`、`${#a[@]}`
- 别名 `alias` / `unalias`、环境变量 `export` / `unset`、`readonly`
- 作业控制：`jobs` / `fg` / `bg`、后台 `&`、`wait`、`trap`
- `[[ ... ]]` 条件表达式：`-n` / `-z` / 文件测试 / `==` `!=` `=~` / 逻辑组合
- `set -e`（errexit，含 `&&`/`||`/`if` 条件的豁免语义）、`set -u`、`set -x`、`pipefail`

### 历史与 `fc`
- 历史文件 `~/.besh_history` 使用 bash 兼容格式，**带时间戳**（`#<epoch>` 行）
- `HISTTIMEFORMAT`（如 `'%F %T '`）控制 `history` 输出中的时间渲染
- `HISTFILE` / `HISTSIZE` / `HISTFILESIZE` / `HISTIGNORE` / `histignoredups` 选项
- `history [-c] [-d pos] [-a] [-r] [-w] [-N]`，`-d` 按 1-based 位置删除
- `fc -l`（列出）、`fc -s [old=new] [cmd]`（替换后重跑）、
  `fc -e [editor] [first] [last]`（编辑后执行）；
  编辑器取自 `-e` 参数、`$FCEDIT`、`$EDITOR`，最后回落到 `vi`
- `fc` 不会把自己留在历史尾部（`fc -s` 不会自我递归）

### 可编程补全
- `complete [-W wordlist] [-F func] [-A action] [name ...]`、`complete -p` / `-r`
- `compgen [-W ...] [-F ...] [-A ...]`，
  action 支持 `command` `builtin` `function` `alias` `variable` `keyword` `file` `dir`
- 补全函数可**打印候选到 stdout**（每行一个），也可**设置 `COMPREPLY`**（后者优先）
- Tab 键按当前命令查表：命中规则用规则候选，否则回落文件名补全
- 多候选时先补公共前缀，再按列展示

### `command_not_found_handler`
- zsh 风格的命令未命中钩子：定义该函数后，未找到的命令会把**整条命令行**
  作为位置参数交给它，其返回码成为 `$?`
- 未定义该函数时保持原有行为（打印错误并返回 127）

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
- 方向键、Home/End/Delete、Tab 补全（可编程规则优先，然后文件名）
- **UTF-8 感知**：中文可输入/粘贴，**退格与删除按字符（码点）处理**，
  不会在字符中间留下半个字节
- **多行编辑**：`for` / `while` / `until` / `if` / `case` / `{ ...; }` /
  函数定义 / 未闭合引号 / 行尾 `\` 都能跨行输入，二级提示符为 `> `
- 历史保存到 `~/.besh_history`
- **启动配置** `~/.beshrc`：每次交互式启动时执行，可持久化
  `alias` / `abbr` / `setopt` / `PROMPT` / `complete` / 补全函数等设置：

```sh
# ~/.beshrc 示例
alias ll="ls -l"
abbr -a gp "git push"
setopt autocd globstar
PROMPT='%n@%m %~%g%# '
complete -W "start stop restart status" svc
```

## 用法示例

```sh
# 进程替换与花括号组
besh> diff <(sort a.txt) <(sort b.txt)
besh> { echo one; echo two; } > out.txt

# 复合命令重定向
besh> while read -r line; do echo "got $line"; done < input.txt 2> err.log

# 变量字符串操作
besh> p=/usr/local/bin/tool; echo ${p##*/}; echo ${p%.*}
tool
/usr/local/bin/tool

# 条件表达式
besh> [[ -n "$HOME" && "$HOST" == dev* ]] && echo yes

# fish 风格
besh> abbr -a gp "git push"
besh> gp<回车>              # 自动展开为 git push
besh> ec<→或Tab>            # 接受灰色建议 echo ...

# zsh 风格
besh> pushd /var/log && dirs -v
besh> echo **/*.sh          # 递归通配
besh> echo {1..10}          # 范围展开
besh> cd -1                 # 回到目录栈

# 历史与 fc
besh> fc -l                 # 列出最近的命令
besh> fc -s rm=cp           # 把上一条命令里的 rm 换成 cp 再跑
besh> fc -e vi 10 12        # 编辑第 10-12 条再执行

# 可编程补全
besh> complete -W "alpha beta gamma" mycmd
besh> mycmd al< Tab >       # 补全成 alpha
besh> complete -p           # 查看已注册规则

# 命令未命中提示
besh> command_not_found_handler() { echo "hint: try: apt install $1" >&2; return 127; }
besh> ripgrep                # 交给上面的函数处理
hint: try: apt install ripgrep
```

## 测试

项目自带两套自动化测试，都接入 `make test`：

| 套件 | 位置 | 说明 |
|------|------|------|
| 差分套件 | `tests/cases/`、`tests/run.sh` | 每个用例同时交给 **bash** 和 **besh** 执行，stdout+stderr 逐字节比对；只有完全一致才算通过 |
| 交互式套件 | `tests/interactive.py` | 通过 pty 驱动真实终端，覆盖行编辑器、历史/`fc`、Tab 补全、`command_not_found_handler` |
| besh 专属用例 | `tests/only/` | 针对 bash 没有的 zsh 特性，用文件内的 `--- expect ---` 标记写死期望输出 |

```sh
make test                 # 全部
make test-diff            # 差分套件
make test-interactive     # 交互式套件
tests/run.sh -v           # 失败时打印 diff
BESH=./besh tests/run.sh  # 指定被测二进制
```

## 项目结构

| 文件 | 职责 |
|------|------|
| `lexer.c`   | 词法分析：分词、引号、heredoc、进程替换嗅探、fd 前缀重定向 |
| `parser.c`  | 递归下降语法分析，生成 AST |
| `executor.c`| AST 执行：管道、重定向、作业控制、展开集成 |
| `expand.c`  | 变量/波浪号/花括号/通配/命令替换/进程替换展开 |
| `builtins.c`| 全部内建命令（cd、abbr、pushd、setopt、read、fc、compgen、complete …） |
| `main.c`    | REPL、行编辑器（高亮/建议/搜索/多行）、提示符、历史、信号 |
| `shell.h`   | 公共头文件与全局状态 |
| `tests/`    | 差分套件与交互式套件 |

`web/` 目录是一个展示 besh 的 Next.js 单页落地页（终端模拟、语法高亮演示、
自动建议演示、迷你游戏等）。

```sh
cd web
npm install && npm run dev   # http://localhost:3000
npm run build                # 静态导出到 web/out（GitHub Pages 部署）
```

## 与 bash / zsh 的差距

以下是从前的限制，**现在均已实现**，在此列出以便对照：

| 曾经的限制 | 现状 |
|------------|------|
| 交互模式下复合命令只能写成单行 | 已支持跨行 `for` / `while` / `if` / `case` / 函数 / `{ }` |
| 行尾 `\` 续行不支持 | 已支持（交互式、`-c`、脚本、管道输入均可） |
| 数组、`local`/`declare` 不支持 | 已支持，含 `declare -a` / `typeset` / 函数 `local` |
| `[[ ]]` 不支持 | 已支持完整条件表达式（含 `=~`） |
| `${var#pat}` 等字符串操作不支持 | 已支持 `#` `##` `%` `%%` `/` `//` 及 `${#var}` |
| `set -e` 语义不支持 | 已实现，含 `&&`/`\|\|`/`if` 条件的豁免规则 |
| 进程替换 `<(...)` 不支持 | 已支持 `<(...)` 与 `>(...)` |
| 退格按字节处理，多字节字符会残留半个字节 | 已改为按码点处理 |
| 历史无时间戳、无 `fc` | 已有 bash 格式时间戳，`fc -l/-s/-e` 全部可用 |
| `fc`、`compgen`、`command_not_found_handler`、补全脚本未实现 | 均以实现，Tab 已接入可编程补全 |
| `read -t` 接受参数后静默忽略（退化为阻塞读） | 已实现真正的超时（`select` + 期限），超时返回 142，参数非法即报错 |
| `${#var#pat}` 静默返回 0 | 与 bash 一致报 `bad substitution`，非交互模式下终止脚本 |

### 已修复的正确性缺陷

这些都曾真实存在，多数由测试（含 AddressSanitizer）复现后修掉：

| 缺陷 | 说明 |
|------|------|
| heredoc 队列栈溢出 | 同一行 `<<` 超过 4 个（`cat <<A <<B <<C <<D <<E`）会越界写栈数组并崩溃；队列已改为按需增长 |
| 一行多个 heredoc 被吞 | `cat <<A <<B` 只读取第一个；现在按声明顺序读取全部，最后一个成为 stdin（与 bash 一致） |
| 引号 / 反斜杠定界符失效 | `<<'EOF'`、`<<\EOF`、`<<"EOF"` 无法匹配结束行；且 `<<\EOF` 的正文被错误地展开 |
| 进程替换 fd 泄漏 | `<(...)` 用做命令参数时管道端从不由调用方关闭，循环里 40 次迭代泄漏 80 个 fd（bash 保持 4 个） |
| 进程替换子进程成僵尸 | `psub_pids[]` 只写不读，从不 `waitpid`；实测 30 次迭代累积 30 个僵尸进程。现在每条命令结束统一 `close` + `waitpid` |
| 进程替换静默丢弃 | 超过 `MAX_PSUB`(128) 的记录被丢弃且不回收；改为动态数组，200 个替换也不泄漏 |
| `echo -e '\c'` 丢数据 | 提前返回时不 `fflush`，重定向到文件时字节留在 stdio 缓冲，文件为空 |
| Ctrl-R 无法回溯 | 上次匹配结果被循环顶部重置，反复按 Ctrl-R 始终停在最新一条 |
| 花括号展开堆溢出 | `{a,b}{1,2}` 与 `pre{1..2}post` 在写入 `NULL` 终止符时越界写一个指针（ASan 发现） |
| range 后的花括号不展开 | `{1..5}{a,b}` 输出成 `1{a,b} 2{a,b} …`，字面残留 |
| 重定向目标不展开 | `echo hi > "$f"` 不建文件并报 `File exists` |
| 重定向失败仍执行 | `open()` 失败被 `if (fd >= 0)` 吞掉，命令照常运行 |
| `noclobber` 对内建无效 | `set -C` 下 `echo x > existing` 照样覆盖 |
| `$?` 在双引号内打印字面量 | 词法层把 `?` 转义，展开器认不出参数 |
| `command_not_found_handler` 收不到参数 | 契约改为 `argv[0]` 即 `$1` |
| `fc -s` 自我递归 / `fc -e -1` 选错条目 | REPL 把 `fc -s` 自身写入历史后被重新执行 |
| `help` 列表与注册表不同步 | 硬编码声称 41 个内建而实际 44 个，改为从注册表生成 |
| `-O2` 下静默截断 | `prompt_render` 与 `gen_files` 把 `MAX_PATH` 长的串写入同样大小的缓冲；CI 曾用 `-Wno-format-truncation` 掩盖，该开关已移除 |

## 测试

```sh
make test              # 差分套件 + 交互套件 + 演示
make test-diff         # 38 个用例，逐个与 bash 对比 stdout+stderr
make test-interactive  # 17 个 pty 检查（行编辑器、历史、补全）
make asan              # 用 ASan + UBSan 重建
ASAN_OPTIONS=detect_leaks=0 tests/run.sh   # 在 sanitizer 下跑差分套件
```

`tests/cases/` 下的每个用例都会同时交给 `bash` 和 `./besh` 执行，
输出逐字节对比 —— 一致性本身即是断言。`tests/only/` 存放 bash
没有对应物的 zsh 风格特性，期望输出内嵌在文件里。

CI 会以 `-Werror` 构建、跑两个套件，再用 ASan/UBSan 重跑一遍：
差分测试只能证明 besh 与 bash *输出相同*，看不出「越界写之后恰好
产生正确字节」的缺陷，而上面表里有两处正是这种情况。

仍然存在的、有意为之的差异（不打算追平）：

- **`$-` 只列出 besh 实际追踪的选项**（`e f a u v x i C`），
  不包含 bash 的 `h`（hashall）与 `B`（braceexpand），因为二者在 besh 中无常量语义
- **`history` 只在交互式会话中累积**（与 bash 一致）：脚本文件与 `-c` 中为空
- **补全不实现 bash 的 `-o` 选项族**（`filenames`、`nospace`、`default` 等），
  只按 `-W` / `-F` / `-A` 三类来源产生候选
- `[[ ... ]]` 的 `=~` 使用 POSIX ERE（`regcomp`/`regexec`），
  不支持 bash 的 `BASH_REMATCH` 捕获数组

日常轻量使用与大多数 shell 脚本都可胜任；遇到依赖上述 bash 特有行为的
重型脚本，请继续使用 bash/zsh。

## 许可

见 [LICENSE](LICENSE)。
