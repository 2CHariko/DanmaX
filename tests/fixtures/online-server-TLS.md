# 本地 HTTPS 测试证书

`online-server-cert.pem` 与 `online-server-key.pem` 为本项目使用 .NET
`CertificateRequest` 生成的自签名 RSA 测试材料，覆盖 localhost / 127.0.0.1，
有效期 2020–2040。私钥公开，仅供回环模拟服务测试，不能用于真实服务。

测试仅将证书加入当前测试进程的 Qt CA 配置，不修改 Windows 证书存储；
随后恢复配置并验证不受信任的服务被拒绝。未引入 OpenSSL 工具或运行库。
