# DFONE Windows USB RNDIS 安装说明

DFONE USB 虚拟网口使用 Windows 内置 RNDIS 驱动。正常安装后，设备会出现在：

```text
设备管理器 -> 网络适配器 -> DFONE USB RNDIS Network Adapter
```

如果显示在：

```text
其他设备 -> RNDIS
```

说明 Windows 已经枚举到 USB 设备，但没有自动绑定网络驱动。

## 推荐的现场安装方式

1. 打开设备管理器。
2. 右键 `其他设备 -> RNDIS`。
3. 选择 `更新驱动程序`。
4. 选择 `浏览我的电脑以查找驱动程序`。
5. 选择 `让我从计算机上的可用驱动程序列表中选取`。
6. 设备类型选择 `网络适配器`。
7. 厂商选择 `Microsoft` 或 `Microsoft Corporation`。
8. 型号选择 `Remote NDIS Compatible Device`。
9. 确认安装。

安装完成后，检查：

```powershell
ipconfig
ping 192.168.7.2
```

Windows 侧 USB 网卡应拿到：

```text
IPv4 address: 192.168.7.1
Mask:         255.255.255.0
```

板端地址固定为：

```text
192.168.7.2
```

## 使用本目录 INF

如果 Windows 手动列表里找不到 `Remote NDIS Compatible Device`，可以尝试：

1. 右键 `RNDIS`，选择 `更新驱动程序`。
2. 选择 `浏览我的电脑以查找驱动程序`。
3. 选择 `从磁盘安装`。
4. 选择本目录的 `dfone_rndis.inf`。

注意：当前 INF 用于开发阶段的 `VID_1D6B&PID_0104`。正式客户发布时，应替换为 DFONE 自有 VID/PID 并签名驱动包。

如果安装时出现：

```text
第三方 INF 不包含数字签名信息
```

说明 Windows 拒绝安装未签名的 `dfone_rndis.inf`。这不代表 DFONE 的 USB
虚拟网口不能用，而是当前开发 INF 没有配套的签名 `.cat` 文件。

开发阶段优先规避方式：

1. 不使用本目录 INF。
2. 在设备管理器中手动选择 Windows 内置驱动：
   `网络适配器 -> Microsoft/Microsoft Corporation -> Remote NDIS Compatible Device`。
3. 安装完成后检查 `ipconfig` 和 `ping 192.168.7.2`。

客户发布版本应准备签名驱动包：

1. 使用 DFONE 自有 VID/PID。
2. 用 WDK `inf2cat` 为 INF 生成 `.cat`。
3. 通过 Microsoft Hardware Developer Center 做 attestation/WHQL 签名。
4. 发布 `INF + CAT`，实际 RNDIS `.sys` 仍使用 Windows 内置驱动。

不建议客户使用测试模式或关闭驱动签名强制。该方式只适合内部调试机器。

## 如果仍然不能工作

在 Windows 上先卸载旧枚举缓存：

1. 设备管理器中右键 `RNDIS`。
2. 选择 `卸载设备`。
3. 如果有 `删除此设备的驱动程序软件`，勾选。
4. 拔掉 USB，等待几秒后重新插入。

在板端确认 gadget 和网络：

```sh
/etc/init.d/S45usb-gadget restart
ls /sys/class/net/usb0
ifconfig usb0
cat /sys/kernel/config/usb_gadget/dfone/idVendor
cat /sys/kernel/config/usb_gadget/dfone/idProduct
```

期望：

```text
usb0 存在
usb0 = 192.168.7.2/24
idVendor = 0x1d6b
idProduct = 0x0104
```
