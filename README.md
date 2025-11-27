# Rusticaland Server Checker

Rusticaland Server Checker is a simple diagnostic tool that shows whether you can connect to Rusticaland services and what might be causing issues.

## Features
- Checks internet availability and response time to Google
- Checks Rusticaland website response
- Checks Rusticaland servers (Vanilla, Sandbox, OneGrid, Monthly, Modded)
- Detects VPN/Proxy locally and via external check
- Provides firewall hints (rules, hosts file)
- Copy results to clipboard (Discord-friendly code block)

## System Requirements
- Windows 10/11

## Usage
1. Run `Rusticaland Server Checker.exe`.
2. The tool will display diagnostics step by step:
   - Internet status and response time
   - Rusticaland website response
   - Server responses (5× per server, min/avg/max)
   - VPN/Proxy detection (local + external)
   - Firewall and hosts hints if something is blocked
3. At the end:
   - Press `C` to copy the output (Discord-ready)
   - Press `X` to exit

## Notes
- Running as administrator may improve firewall diagnostics.
- Share the copied output with Rusticaland admins to get targeted help.
