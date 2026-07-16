# Git 身份切换机制

> 跨项目 commit 作者身份自动按路径切换。**默认 = chencong**，少数沙箱 / Codex
> 测试夹具 = Codex Local。所有规则集中在 `~/.gitconfig` 的 `[includeIf "gitdir:..."]` 段。

## 路由规则（单文件）

`~/.gitconfig` 顶部仅路由，不放 `[user]` 段。

| 路径前缀 | 身份 | 配置文件 |
|---|---|---|
| `D:/sandbox/` | Codex Local &lt;codex-local@example.invalid&gt; | `~/.gitconfig_codex` |
| `D:/working_file/.codex/` | Codex Local | `~/.gitconfig_codex` |
| `D:/working_file/` | chencong &lt;cong.chen@cd-excite.cn&gt; | `~/.gitconfig_chencong` |
| 其它 (`D:/` 兜底) | chencong | `~/.gitconfig_chencong` |

## profile 文件

| 文件 | 内容 |
|---|---|
| `~/.gitconfig_chencong` | `[user] name=chencong email=cong.chen@cd-excite.cn` |
| `~/.gitconfig_codex` | `[user] name=Codex Local email=codex-local@example.invalid` |

## 关键陷阱

1. **`[user]` 不要写在主 `~/.gitconfig`**，否则它会胜出 include 加载的段。
   兜底身份只能通过 includeIf 路由表达。
2. **多 includeIf 同时匹配时**，git 按"加载顺序"取最后一个；如果想让"更具体的路径"
   优先匹配并胜出，把它放在兜底 includeIf 之后（git 实际从下往上 evaluate 段优先级）。
3. **仓库 local `.git/config` 优先级最高**——单仓库临时切身份可直接
   `git config --local user.name ...`，但建议先把规则加进全局再删 local。

## 新增一类身份的流程

1. 写一份 profile 文件：
   ```bash
   cat > ~/.gitconfig_<name> <<EOF
   [user]
   	name = ...
   	email = ...
   EOF
   ```
2. 在 `~/.gitconfig` 顶部加一条 `[includeIf "gitdir/i:<路径>/"] path = ~/.gitconfig_<name>`
3. 用 `git config --show-origin --get user.name` 在该路径下的临时仓库验证

## 验证命令

```bash
git config --show-origin --get user.name
git config --show-origin --get user.email
git var GIT_AUTHOR_IDENT       # 当前仓库 author 会用的身份
git var GIT_COMMITTER_IDENT    # committer 同 author
```

## 禁用 / 还原

若要全部回到系统 Git 默认行为（无任何 identity）：

```bash
mv ~/.gitconfig ~/.gitconfig.bak
mv ~/.gitconfig_chencong ~/.gitconfig_chencong.bak
mv ~/.gitconfig_codex ~/.gitconfig_codex.bak
```

恢复：

```bash
mv ~/.gitconfig.bak ~/.gitconfig
mv ~/.gitconfig_chencong.bak ~/.gitconfig_chencong
mv ~/.gitconfig_codex.bak ~/.gitconfig_codex
```
