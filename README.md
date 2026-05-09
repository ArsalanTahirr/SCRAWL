# SCRAWL 🕷️

<p align="center">
  <b>A Lightning-Fast, Multithreaded Web Crawler in C (CLI & GUI)</b><br>
  <i>Developed by Arsalan Tahir</i>
</p>

---

## 🚀 Overview

**SCRAWL** is a highly concurrent, lock-safe, and memory-efficient web crawler built from scratch in C11. It is designed to navigate the web rapidly using `libcurl` and `pthreads`, aggressively fetching HTML, tracking URL states, and scraping domain linkages in real time. 

SCRAWL ships with two complete front-ends:
1. **The CLI:** A headless, pipeline-ready command line interface.
2. **The GUI:** A stunning, fully interactive dashboard built with **Raylib**, featuring live-updating progress bars, dynamic log tables, and custom-built native text fields.

## ✨ Features

- **Blazing Fast Concurrency:** Built entirely on POSIX threads (`pthreads`) using advanced lock-free atomics and condition variables to eliminate race conditions.
- **Robust Politeness:** Native server-side `robots.txt` parsing and memory-efficient local caching ensure the crawler respects site rules automatically.
- **Domain Scoping:** Optionally lock the crawler to only scrape links belonging to the initial seed domain.
- **Data Export:** Outputs structured, timestamped crawl logs to a `.jsonl` file.
- **Asset Filtering:** Smart logic skips static assets (`.css`, `.js`, `.png`) to save bandwidth and page quotas.
- **Built-in Dashboard:** The GUI mode features dynamic scrolling, clipboard shortcuts (`Ctrl+C/V/X`), and real-time metric tracking.

---

## 🛠️ Build Instructions

### Dependencies
Before building, ensure you have the required development libraries installed:
*   `gcc` (C11 support)
*   `libcurl` (Networking)
*   `raylib` (For the GUI build)
*   `pthread` (Standard POSIX threads)

**Ubuntu/Debian Setup:**
```bash
sudo apt-get update
sudo apt-get install build-essential libcurl4-openssl-dev
# You must also install Raylib from source or package manager for the GUI.
```

### Compilation
The provided `Makefile` manages the distinct builds cleanly.

**Build the CLI:**
```bash
make all
```

**Build the GUI Dashboard:**
```bash
make gui
```

**Clean Build Artifacts:**
```bash
make clean
```

---

## 💻 Usage

### Command Line Interface (CLI)

The CLI is perfect for scripting and headless server deployments.

```bash
./crawler [OPTIONS] <seed-url>
```

**Options:**
| Flag | Description | Default |
| :--- | :--- | :--- |
| `-t <N>` | Number of concurrent worker threads | `4` |
| `-n <N>` | Maximum number of HTML pages to fetch | `1` |
| `-o <file>` | Output results to a JSONL file | *(none)* |
| `-d` | Enable Domain Scope (do not leave seed domain) | Disabled |
| `-v` | Enable verbose terminal logging | Disabled |

**CLI Example:**
*Crawl up to 500 pages on example.com using 8 threads and save to results.jsonl:*
```bash
./crawler -t 8 -n 500 -o results -d "https://example.com"
```

*Note: You can also use `make run FLAGS="..." URL="..."` to run the crawler safely.*

### Graphical Interface (GUI)

The Raylib GUI provides a gorgeous visual experience to configure and monitor your crawl in real-time.

```bash
./crawler-gui
```
- **Seed URL:** Fully supports keyboard navigation, `Home`/`End` keys, and clipboard pasting (`Ctrl+V`).
- **Live Logs:** Features automatic column resizing and dynamic `...` truncation for extra-long URLs.
- **State Control:** Allows instantly stopping the current crawl or resetting the UI to begin a fresh crawl safely.

---

## 🏗️ Architecture & Internal Mechanics

SCRAWL is engineered for strict memory safety and zero data-races:
*   **Hash Table:** Uses a custom DJB2 hash-table under a Reader-Writer lock (`pthread_rwlock_t`) for extremely fast O(1) duplicate URL detection.
*   **Condition Variables:** The crawler safely throttles worker threads using `pthread_cond_wait` when the `max_pages` limit is reached to prevent over-fetching.
*   **Memory Management:** The codebase is relentlessly audited. All parsed HTML chunks, queue nodes, and curl handles are correctly drained and freed regardless of connection failures or early terminations.

---

## 📝 License

Designed and Maintained by **Arsalan Tahir**.
All Rights Reserved.
