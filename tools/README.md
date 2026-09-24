# tools/ —— 辅助工具（不属于运行时依赖）

这些脚本用于排查与运维，**不参与** `client/`、`go2_network_diag/` 的构建。

| 文件 | 用途 | 用法 |
|---|---|---|
| `key_try.py` | 验证候选 AES-128 钥匙是否属于某台狗 | `python3 key_try.py <IP> <32位hex>...` |
| `key_verify.py` | 同上，验证通过后写入客户端钥匙缓存 | `python3 key_verify.py <IP> <hex>` |
| `tab_dogs.py` | 从平板 App 日志批量提取连接过的狗与钥匙 | `python -X utf8 tab_dogs.py` |
| `key_extract.py` | 从机器狗内网取钥匙（需先有 shell 访问） | 见文件头注释 |
| `diag_pro.py` | 用参考实现（aiortc）做对照诊断 | `python3 diag_pro.py <IP>` |
| `recover.py` | 指令全被拒（code=-1）时的诊断与恢复流程 | `python3 recover.py <IP>` |
| `unitree_cloud.py` | 宇树云账号登录 → 拉取账号下设备钥匙 | `python3 unitree_cloud.py --help` |
| `con_notify.py` | 读 `con_notify` 明文，判断固件/是否需要每设备钥匙 | `python3 con_notify.py <IP>` |
| `dds_raw.py` | DDS 侧原始探测（bashrunner / programming_actuator 线索） | 见文件头注释 |
| `pcap_viewer.py` | 抓包文件（pcap）查看与统计 | `python3 pcap_viewer.py <file.pcap>` |

> ⚠️ `unitree_cloud.py` 会访问宇树官方服务器，请自行确认后手动执行。
> ⚠️ 钥匙文件（`keys.json` / `keys.txt` / `*_keys*.json`）一律不要提交到版本库。
