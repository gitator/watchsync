# Installation and Configuration Guide

This guide provides step-by-step instructions to install `watchsync` as a system service.

## 1. Compilation

Ensure you have the necessary build tools and dependencies:

```bash
sudo apt update
sudo apt install build-essential rsync sshpass
```

Build the binary:

```bash
make
```

## 2. Installing Binaries and Configuration

Install the binary to `/usr/local/bin` and create the base configuration structure:

```bash
sudo make install
```

The installation creates:
- `/usr/local/bin/watchsync`
- `/etc/watchsync.conf` (global configuration)
- `/etc/watchsync.d/` (directory for sync jobs)

### Important: Permissions

For security reasons, `watchsync` requires that configuration files containing potentially sensitive information (like passwords) are not accessible by others.

*Note: In command mode (`-c`), you can omit the password from the configuration and you will be prompted to enter it securely.*

Set restricted permissions:
```bash
sudo chmod 600 /etc/watchsync.conf
sudo chmod 700 /etc/watchsync.d
```

## 3. Systemd Service Setup

To run `watchsync` as a system-managed daemon:

1. Install the systemd unit file:
   ```bash
   sudo cp etc/watchsync.service /etc/systemd/system/
   ```

2. Reload systemd and start the service:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable watchsync
   sudo systemctl start watchsync
   ```

## 4. Verification

Check the service status:

```bash
systemctl status watchsync
```

Monitor logs in real-time:

```bash
journalctl -u watchsync -f
```
