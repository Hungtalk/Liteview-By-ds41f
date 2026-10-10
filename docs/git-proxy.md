# git 网络配置（代理 / TLS）/ Git behind a proxy

本文件只讲**git 访问 GitHub 的网络配置**；工具链与构建说明见
[development.md](development.md)。

---

## 一、症状与原因（中国大陆网络环境）

```bash
fatal: unable to access 'https://github.com/...': Recv failure: Connection was reset
```

```bash
fatal: unable to access 'https://github.com/...': schannel: the certificate chain is incomplete
```

| 报错 | 原因 |
|---|---|
| `Recv failure: Connection was reset` | 直连被网络层重置 |
| `schannel: the certificate chain is incomplete` | 走了代理，代理做了 **TLS 中间人**，而 Git 默认的 schannel 后端拿不到完整证书链（`openssl` 后端会报 `unable to get local issuer certificate`） |

## 二、排查步骤

```bat
:: 1) 是否有代理在监听？本机常见为加速器（Steam++ / Watt Toolkit）占用某端口
Get-NetTCPConnection -State Listen | Where-Object LocalPort -in 26561
Get-Process -Id (Get-NetTCPConnection -State Listen -LocalPort 26561).OwningProcess

:: 2) 系统（WinINET）层的代理设置
Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' |
  Select-Object ProxyEnable, ProxyServer, AutoConfigURL

:: 3) git 当前是否配了代理
git config --list --show-origin | findstr /i "proxy ssl"
```

> 注意：系统装了代理**不代表 git 会自动走它**。git 只认
> `http.proxy` / `HTTP(S)_PROXY` 环境变量，不会读 WinINET 设置。

## 三、配置（只对 github.com 生效）

一次性写入 `~/.gitconfig`：

```bat
git config --global http.https://github.com.proxy http://127.0.0.1:26561
git config --global http.https://github.com.sslBackend schannel
git config --global http.https://github.com.schannelUseSSLCAInfo false
git config --global http.https://github.com.sslVerify false
```

| 配置 | 作用 |
|---|---|
| `http.https://github.com.proxy` | **URL 级**代理，仅 github.com 走本地加速器，不影响其它站点 |
| `...sslBackend schannel` | 使用 Windows 原生 TLS 后端（走系统证书库） |
| `...schannelUseSSLCAInfo false` | 忽略 `sslCAInfo` 指定的 CA 文件，改用 Windows 根证书库。**这条是关键**：加速器的根证书装在系统证书库里，而不是 git 自带的 `ca-bundle.crt` |
| `...sslVerify false` | 兜底关闭校验，**最后手段**；若代理根证书已正确装入「受信任的根证书颁发机构」，建议删掉这一行 |

验证：

```bat
git ls-remote --heads origin
git fetch --dry-run origin
```

只对单次命令生效（不改配置）：

```bat
git -c http.proxy=http://127.0.0.1:26561 -c http.schannelUseSSLCAInfo=false fetch origin
```

## 四、凭证安全

- **不要把 Personal Access Token 写进远程 URL，也不要提交到仓库。**
- 本机凭证由 **Git Credential Manager** 管理（`credential.helper=manager`，
  配置在 Git for Windows 自带的 `etc\gitconfig` 里），首次认证后缓存到
  Windows 凭据管理器，之后 `git pull` / `git push` 无需再手动提供 token。
  查看当前缓存：

  ```bat
  echo protocol=https& echo host=github.com& echo. | git credential fill
  ```

- 若 token 曾出现在聊天记录、日志或命令行里：立即到
  <https://github.com/settings/tokens> **撤销并重新签发**。撤销后凭据管理器里的
  缓存会失效，下次操作会提示重新登录。
- 需要「无密码」自动推送时，优先用**部署密钥（deploy key）**或
  细粒度 token 并限制仓库范围，而不是账号级 token。

## 五、相关：GitHub Actions 推送

若推送工作流文件（`.github/workflows/*.yml`）报权限错误，说明当前 token 缺少
`workflow` 作用域。用细粒度 token 时需勾选 **Workflows: Read and write**；
用经典 token 时需勾选 `workflow`。
