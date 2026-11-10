# Security policy

## Supported versions

Only the latest release of Euclase Monitor receives security fixes.

## Reporting a vulnerability

Please do not publish exploit details in a public issue.

1. Use GitHub private vulnerability reporting: the **Security** tab of this repository, then **Report a vulnerability**.
2. If that option is not available, open an issue titled "Security contact request" without technical details, and a private channel will be arranged.

Expect an acknowledgement within a few days. This is a small project maintained by one person; there is no bug bounty.

## Scope

In scope: the HTTP metrics export, the skin loader (JSON parser, expression evaluator, image paths), the hotkey and tray code, and the release archive.

## Security-relevant design

- The metrics export is **off by default**. When enabled it binds to `127.0.0.1`. LAN mode is a separate explicit setting.
- The export has **no authentication**. Do not expose it to an untrusted network.
- In LAN mode the process list is never included in responses.
- Skins are data, not code: the expression language has no file, network or process access. Image paths must stay inside the skin folder.
- The program makes no network connections other than the optional export, and sends no telemetry.
- The release executable is not code-signed yet. Verify the SHA-256 published with each release.
