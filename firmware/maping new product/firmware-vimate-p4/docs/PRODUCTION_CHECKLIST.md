# VIMATE Firmware Production Checklist

## Security gates

- Build release with `scripts/build_production.sh`.
- Use a production-only Secure Boot signing key stored offline.
- Burn Secure Boot V2 and Flash Encryption only during controlled factory provisioning.
- Verify NVS Encryption is enabled with an encrypted `nvs_key` partition.
- Use per-device tokens only. Revoke/rotate token from server when a device is lost or transferred.
- Keep UART logs at WARN or lower in production.
- Do not enable flash coredumps on customer builds because RAM can contain tokens.

## Smoothness gates

- Media uploaded for lessons must be optimized JPEG, maximum 480x360, target under 180KB.
- SD pins must be configured in `menuconfig -> VIMATE` before claiming SD cache support.
- Run `scripts/build_soak.sh`, flash a test device, then run `scripts/soak_monitor.sh <port>` for at least 12 hours.
- Pass: no unexpected reset, no watchdog, no heap downward trend, BOOT barge-in responds consistently.

## Factory notes

- Secure Boot/Flash Encryption are one-way eFuse operations. Test on sacrificial devices first.
- Keep one debug SKU/profile without release eFuses for lab diagnosis.
- Bump `CONFIG_BOOTLOADER_APP_SECURE_VERSION` only when older firmware must be blocked.
