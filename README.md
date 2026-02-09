# WatchSync

**WatchSync** is a lightweight utility written in C that monitors local directories and synchronizes changes in real-time to a remote server using `rsync`. It is designed to be efficient, easy to configure, and compatible with development and system administration workflows.

**Author**: [Toni Garcia](mailto:antonio.garcia@uji.es)

## Features

*   **Real-time Synchronization**: Uses `inotify` to detect changes instantly.
*   **Daemon Mode**: Can run in the background as a system service.
*   **Flexible Configuration**: Supports multiple "jobs" with global configuration inheritance.
*   **Secure**: Uses `mkstemp` for temporary files and supports internal password management for `sshpass`.
*   **Permission Checks**: Refuses to run with insecurely configured files (e.g., world-readable passwords).
*   **Lightweight**: Written in pure C with minimal dependencies.

## Installation

### Prerequisites

*   GCC or another C compiler.
*   `rsync` installed on both local and remote machines.
*   `sshpass` (optional, for automated password-based sync).
*   Linux system (uses `inotify`).

### Build and Install

1. Clone the repository or download the source.
2. Build the project:
   ```bash
   make
   ```
3. Install binaries and basic configuration files:
   ```bash
   sudo make install
   ```
   *Note: Default permissions for configuration will be set to be restricted.*

## Configuration

### File Structure

*   `/etc/watchsync.conf`: Global configuration. Define `rsync.*` and `exclude` options here to be inherited by all jobs.
*   `/etc/watchsync.d/`: Directory for job-specific configurations. Each file in this directory defines a synchronization job.

### Configuration Variables

| Variable | Description | Scope |
| :--- | :--- | :--- |
| `local.root` | Local directory to monitor | Local |
| `remote.user` | Remote server username | Local |
| `remote.host` | Remote server address | Local |
| `remote.root` | Destination directory on the remote server | Local |
| `remote.password` | Password for the remote user (requires `sshpass`) | Local |
| `rsync.delete` | If `true`, delete files on remote not present locally | Global/Local |
| `rsync.delay_ms` | Wait time after the last event before launching rsync (ms) | Global/Local |
| `exclude` | Exclusion pattern (one per line) | Global/Local |

### Security Requirements

Configuration files and the `/etc/watchsync.d/` directory **must not** be accessible by group or others (e.g., permissions should be `0600` or `0700`). The utility will refuse to load insecure configurations.

### Example: `/etc/watchsync.d/my_project.conf` 

```ini
local.root = /home/user/progs/my_app
remote.user = dev
remote.host = server.example.com
remote.root = /var/www/my_app
remote.password = s3cr3t

# Override global inheritance if needed
rsync.delay_ms = 300
exclude = .git
exclude = vendor
```

## Usage

### Command Mode (Testing)

Run `watchsync` with a specific configuration file in the **foreground**:

```bash
$ watchsync -c ./my_config.conf
[cli] === RSYNC START ===
[cli] <f.st...... User_Manual.md
[cli] <f.st...... README.md
[cli] <f.st...... src/Controller/Authentication/LoginController.php
[cli] === RSYNC OK ===

```

*Note: If `remote.password` is not defined in the configuration file, you will be securely prompted to enter it.*

### Daemon Mode (Production)

To run all jobs defined in `/etc/watchsync.d/` in the background:

```bash
sudo watchsync -d
```

### Systemd Integration

1. Copy the service file: `sudo cp etc/watchsync.service /etc/systemd/system/`
2. Start the service:
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable watchsync
   sudo systemctl start watchsync
   ```

## Use Cases

1.  **Server Synchronization**: Maintain an exact mirror of a configuration or assets directory between two servers.
2.  **Remote Development**: Edit files locally with your favorite IDE and let `watchsync` upload them to the test server instantly.
3.  **Continuous Backup**: Sync important documents to a NAS or backup server as they are modified.

## Contributing

Contributions are welcome! Feel free to open an Issue or a Pull Request for performance improvements, new change detection backends, better error handling, or **alternative authentication methods** (e.g., SSH keys, different vault integrations).

## License

This project is licensed under the [MIT License](LICENSE).
