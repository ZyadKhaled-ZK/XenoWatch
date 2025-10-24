# XenoWatch

XenoWatch is a lightweight Windows C++ application that monitors the activity of XenForo forum members. It periodically checks the "Last seen" status on a member's profile and logs any changes, allowing you to track activity in real-time.

---

## Features

- Monitor XenForo member activity in real-time
- Configurable polling interval
- Logs activity to a file with timestamps
- Graceful shutdown support (Ctrl+C)
- Optional verbose logging for debugging
- Exponential backoff on consecutive failures
- Optional SSL certificate check override (for testing only)

---

## Installation

1. Clone the repository:
git clone https://github.com/YourUsername/XenoWatch.git

2. Open the project in Visual Studio (tested with VS2019/VS2022)

3. Build the project:
   - Choose Release x64 for the final executable

---

## Configuration

The application uses a `config.ini` file for settings. If it doesn't exist, XenoPulse will generate a default one.

Example `config.ini`:

# Activity Watcher Configuration

# URL to monitor
url=https://forums.example.com/members/username/

# Poll interval in seconds (minimum 5)
poll_interval=10

# Log file path
log_file=activity_log.txt

# Ignore SSL certificate errors (SECURITY RISK - use only for testing)
ignore_ssl_errors=false

# Enable verbose logging
verbose=false

You can also override the URL via command line:

XenoPulse.exe https://forums.example.com/members/username.1/

---

## Usage

1. Run the application:
XenoPulse.exe

2. The program will poll the target URL at the configured interval.

3. Updates are logged to activity_log.txt:

[2025-10-24 12:30:00] Activity updated: Last seen today at 15:45

4. Press Ctrl+C to stop the watcher gracefully.

---

## Notes & Warnings

- Only tracks publicly visible activity on XenForo profiles
- Disabling SSL verification is not recommended except for testing
- Respect forum rules and user privacy

---

## License

This project is licensed under the MIT License. See the LICENSE file for details.
