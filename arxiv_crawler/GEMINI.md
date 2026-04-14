# GEMINI.md

## Project Overview

The **Arxiv Crawler** is a project designed to automate the process of searching and downloading research papers from [arXiv.org](https://arxiv.org/). The application is intended to be a single-file command-line interface (CLI) tool that allows users to specify search keywords and a time range to filter and download relevant articles into a local directory.

### Key Features
- **Keyword Filtering:** Search for articles using specific keywords.
- **Time-based Filtering:** Define a time range for article publication dates.
- **Automatic Download:** Save found articles directly to a specified folder.
- **Rate Limiting:** Implements a buffer between downloads to respect arXiv's access policies (e.g., maximum 5 articles per minute).
- **Detailed Error Handling:** Provides comprehensive error messages for easier troubleshooting.

## Building and Running

This is a single-file Python application. You can run it using the `python` command.

### Usage
```bash
# Download PDF (default)
python crawler.py --keywords "black hole" entropy --start-date 2023-01-01 --output downloads

# Download LaTeX source
python crawler.py --keywords "neural networks" transformer --format latex --max-results 2
```

### Arguments
- `--keywords`: List of keywords to search for. Multiple keywords are combined with `AND`.
- `--start-date`: Start date in `YYYY-MM-DD` format (optional).
- `--end-date`: End date in `YYYY-MM-DD` format (optional).
- `--output`: Directory to save files (default: `./downloads`).
- `--max-results`: Maximum number of papers to download (default: 5).
- `--format`: Download format: `pdf` or `latex` (default: `pdf`).

## Development Conventions

- **Single-File Architecture:** The core logic resides in `crawler.py`.
- **Robust Error Messaging:** The script provides detailed feedback for API queries and downloads.
- **Ethical Crawling:** Implements a **12-second delay** between each paper download to respect the "5 papers per minute" rate limit.
- **Dependencies:** Uses only Python's standard library (`urllib`, `xml.etree.ElementTree`, `argparse`, etc.).
