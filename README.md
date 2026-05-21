# VairagyaEngine

VairagyaEngine is a C++20 web crawler with a built-in lightweight search API.

It can:

- crawl one or more seed URLs
- follow discovered links
- respect or ignore `robots.txt`
- store crawled documents and metadata in RocksDB
- build an in-memory search index from stored pages
- expose a local HTTP API for querying indexed documents

This repository is best thought of as a crawling and document-preparation engine. It is not a distributed production search engine, but it already includes the core pieces needed to crawl pages, persist structured records, and query them locally.

## Features

- URL normalization and validation
- HTTP and HTTPS fetching
- basic HTML/text extraction
- link extraction for recursive crawling
- duplicate and near-duplicate support with hashes and simhash
- RocksDB-backed document storage
- resume support from pending URLs
- recrawl selection from existing database records
- local search API with ranking, snippets, fuzzy matching, and click tracking

## Repository Layout

```text
.
|-- CMakeLists.txt
|-- CMakePresets.json
|-- README.md
|-- CRAWLER_PROJECT_INFORMATION.md
`-- VairagyaEngine
    |-- config
    |   `-- synonyms.json
    |-- external
    |   `-- simHash-cpp
    |-- include
    `-- src
```

Important source areas:

- `VairagyaEngine/src/main.cpp`: CLI entry point and runtime mode selection
- `VairagyaEngine/src/crawler`: crawl engine, frontier, scheduler
- `VairagyaEngine/src/net`: HTTP/HTTPS fetching and response classification
- `VairagyaEngine/src/html`: basic content and link extraction helpers
- `VairagyaEngine/src/storage`: RocksDB integration and persistence
- `VairagyaEngine/src/query`: search indexing, query processing, ranking, snippets
- `VairagyaEngine/src/api/search_api.cpp`: local HTTP search API

## Requirements

- C++20 (I used MSVC for Windows and G++for linux environment)
- CMake 3.20 or newer
- Ninja or another supported CMake generator
- OpenSSL
- RocksDB
- nlohmann_json
- Boost, Boost.URL
- Crow headers available to the compiler for API mode build

Bundled in this repository:

- `VairagyaEngine/external/simHash-cpp`
- `VairagyaEngine/include/utils/argparse.hpp`

## Dependency Notes

### Windows

This repo already contains Windows CMake presets, but the preset file currently points to a machine-specific vcpkg toolchain path:

```text
D:/library_openssl/vcpkg/scripts/buildsystems/vcpkg.cmake
```

If that path does not exist on your machine, update `CMakePresets.json` before building.

The `x64-release` preset also sets a machine-specific `CMAKE_PREFIX_PATH` for Boost.URL. You may need to adjust or remove that as well.

### Crow

The source includes `<crow.h>`, but `CMakeLists.txt` does not currently fetch or locate Crow automatically. That means you must make Crow headers available yourself, for example by:

- installing Crow separately and exposing its include path to the compiler
- adding Crow through your package manager
- vendoring Crow into the project and updating include paths

If `crow.h` is missing, the build will fail.

## Download

Clone the repository:

```bash
git clone https://github.com/BAPPAYNE/VairagyaEngine.git
cd VairagyaEngine
```

## Build

### Build on Windows with the provided presets

Recommended if you use Visual Studio Build Tools, MSVC, Ninja, and vcpkg:

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
```

For a debug build:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Expected output executable:

```text
out/build/x64-release/VairagyaEngine.exe
```

or

```text
out/build/x64-debug/VairagyaEngine.exe
```

### Build manually with CMake

If you prefer not to use the presets:

```bash
cmake -S . -B out/build/manual -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/manual
```

If your dependencies are installed in non-default locations, also pass the appropriate variables, for example:

```bash
cmake -S . -B out/build/manual -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg.cmake> \
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

### Linux

There are `linux-debug`  configure presets, but you still need all native dependencies installed first using below command.

```
sudo apt update && sudo apt install -y \
	libasio-dev \
    build-essential \
    g++ \
    cmake \
    ninja-build \
    git \
    pkg-config \
    libssl-dev \
    librocksdb-dev \
    libboost-all-dev \
    nlohmann-json3-dev \
    zlib1g-dev \
    libbz2-dev \
    liblz4-dev \
    libzstd-dev \
    libsnappy-dev

# Crow installation 
wget https://github.com/CrowCpp/Crow/releases/download/v1.3.2/Crow-1.3.2-Linux.deb
sudo dpkg -i Crow-1.3.2-Linux.deb && rm -r Crow-1.3.2-Linux.deb
```

Typical flow:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
```

or:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

If CMake cannot find Boost.URL, RocksDB, OpenSSL, nlohmann_json, or Crow, install them and/or provide CMake prefix paths.

## Runtime Modes

The binary supports two modes:

- `crawler`: crawl pages and write records to RocksDB
- `api`: load stored documents from RocksDB and serve a local search API

## Quick Start

### 1. Crawl a single site

```bash
VairagyaEngine --mode crawler --domain https://example.com --crawl-links --same-domain --database vairagya_db --threads 4
```

What this does:

- starts from `https://example.com`
- follows extracted links
- stays on the same domain
- writes records into `vairagya_db`
- uses 4 worker threads

### 2. Start the local search API

```bash
VairagyaEngine --mode api --database vairagya_db --port 12345
```

### 3. Query the API

Open:

```text
http://127.0.0.1:12345/search?q=example
```

## Default Behavior with No Arguments

If you run the executable with no CLI arguments, the current code defaults to:

```bash
VairagyaEngine --mode api --port 12345 --database vairagya_db
```

So by default it tries to start the search API, not the crawler.

## CLI Usage

Main options:

| Option | Meaning |
|---|---|
| `--mode` | Runtime mode: `crawler` or `api` |
| `-d`, `--domain` | Crawl a single seed URL/domain |
| `-l`, `--list` | Load seed URLs from a text file |
| `-cd`, `--crawl-database` | Use URLs from the database as crawl seeds |
| `--resume-db` | Resume from saved pending URLs |
| `--remove-duplicates` | Remove duplicate URL records from the database and exit |
| `-cl`, `--crawl-links` | Enable recursive link crawling |
| `-sd`, `--same-domain` | Restrict discovered links to the seed domains |
| `-ir`, `--ignore-robots` | Ignore `robots.txt` checks |
| `-db`, `--database` | RocksDB path, default `vairagya_db` |
| `-t`, `--threads` | Number of crawler worker threads |
| `--port` | API port in `api` mode |
| `-v`, `--verbose` | Enable verbose logging |
| `-oj`, `--output-json` | JSON output file path |
| `-o`, `--output` | TXT output file path |

## Common Commands

### Crawl a single page set recursively

```bash
VairagyaEngine --mode crawler --domain https://example.com --crawl-links --same-domain --threads 8
```

### Crawl seed URLs from a file

Create a text file with one URL per line, then run:

```bash
VairagyaEngine --mode crawler --list seeds.txt --crawl-links --database vairagya_db
```

### Resume an interrupted crawl

```bash
VairagyaEngine --mode crawler --resume-db --database vairagya_db --threads 4
```

### Recrawl URLs already stored in the database

```bash
VairagyaEngine --mode crawler --crawl-database --database vairagya_db --threads 4
```

### Remove duplicate stored URL records

```bash
VairagyaEngine --remove-duplicates --database vairagya_db
```

## Search API

When running in `api` mode, the application loads searchable documents from RocksDB into an in-memory index and exposes HTTP endpoints.

### Endpoints

#### `GET /search`

Query parameters:

- `q`: search text
- `page`: page number, default `1`
- `limit`: results per page, default `10`, max `100`

Example:

```text
http://127.0.0.1:12345/search?q=search+engine&page=1&limit=10
```

Example response shape:

```json
{
  "query": "search engine",
  "page": 1,
  "limit": 10,
  "total": 2,
  "results": [
    {
      "doc_id": 12,
      "title": "Example Title",
      "url": "https://example.com/page",
      "display_url": "https://example.com/page",
      "favicon_url": "/favicon.ico",
      "language": "en",
      "snippet": "Short matching text...",
      "score": 8.42,
      "last_fetched_time": 1710000000,
      "quality_score": 0.91
    }
  ]
}
```

#### `GET /health`

Example:

```text
http://127.0.0.1:12345/health
```

Returns basic API health and searchable document count.

#### `GET /stats`

Example:

```text
http://127.0.0.1:12345/stats
```

Returns simple engine statistics.

#### `GET /click` or `POST /click`

Query parameter:

- `doc_id`: numeric document id

Example:

```text
http://127.0.0.1:12345/click?doc_id=12
```

This increments stored click count for that document.

## Database

The crawler stores data in RocksDB column families, including:

- document identity
- fetch metadata
- parsed text
- content hashes
- link metrics
- quality signals
- presentation fields
- indexing control flags

Default database path:

```text
vairagya_db
```

Important stored behavior:

- pending URLs are saved for `--resume-db`
- click counts are stored under internal keys such as `click:<doc_id>`
- recrawl state is also persisted in the default column family

## What Gets Indexed for Search

The API only indexes documents that pass several checks. Based on the current code, documents are skipped if they are missing core data or if:

- HTTP status is not `200`
- parsed clean text is empty
- `noindex` is true
- the record is marked as duplicate
- spam score is greater than `0.8`

The query path currently includes:

- normalization
- tokenization
- stopword filtering
- simple stemming
- synonym expansion from `VairagyaEngine/config/synonyms.json`
- fuzzy fallback for some unmatched terms

## Seed File Format

A seed file should contain one URL per line, for example:

```text
https://example.com
https://www.wikipedia.org
https://news.ycombinator.com
```

Use it with:

```bash
VairagyaEngine --mode crawler --list seeds.txt --crawl-links
```

## Output Files

The CLI exposes:

- `--output-json`
- `--output`

These are intended for JSON and text output paths. Their exact runtime output depends on the logging flow in the current codebase, so treat them as optional logging/export targets rather than the primary storage mechanism. The main persistent output is RocksDB.

## Limitations

Current limitations worth knowing before use:

- Crow is not auto-configured by CMake
- Windows presets contain machine-specific dependency paths
- redirects are not followed automatically
- SSL verification is currently disabled in the fetcher
- HTML parsing is basic
- search index is in-memory and built at API startup
- this is not a distributed crawler
- large-scale production crawling will need more scheduling, politeness, and resilience work

## Troubleshooting

### Build fails because dependencies are not found

Check:

- `CMAKE_TOOLCHAIN_FILE`
- `CMAKE_PREFIX_PATH`
- package manager installation paths
- whether `crow.h` is actually installed and visible to the compiler

### Build fails on Windows preset

Open `CMakePresets.json` and replace the hardcoded paths with paths that exist on your machine.

### API starts but returns zero results

Usually this means one of these is true:

- the database path is wrong
- no successful pages have been crawled yet
- crawled pages were filtered out during index loading

### `--resume-db` does nothing useful

That mode only resumes URLs saved as pending URLs in RocksDB. If the database has no pending crawl queue stored, there is nothing to resume.

## License

This project is licensed under the terms in [LICENSE](LICENSE).

## Additional Notes

`CRAWLER_PROJECT_INFORMATION.md` contains a deeper handoff-style explanation of the storage model, crawler pipeline, and how another project can build a fuller search engine on top of this database.
