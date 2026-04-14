import argparse
import os
import re
import sys
import time
import urllib.request
import urllib.parse
import xml.etree.ElementTree as ET
from datetime import datetime

# Namespace for Atom feeds
NS = {'atom': 'http://www.w3.org/2005/Atom'}

def sanitize_filename(filename):
    """Remove invalid characters for filenames."""
    return re.sub(r'[<>:"/\\|?*]', '_', filename)

def download_file(url, filepath):
    """Download a file from a URL to a local path."""
    if os.path.exists(filepath):
        print(f"File already exists, skipping: {filepath}")
        return True

    try:
        print(f"Downloading: {url} -> {filepath}")
        with urllib.request.urlopen(url) as response:
            with open(filepath, 'wb') as out_file:
                out_file.write(response.read())
        print(f"Successfully downloaded: {filepath}")
        return True
    except Exception as e:
        print(f"Error downloading {url}: {e}", file=sys.stderr)
        return False

def parse_arxiv_xml(xml_content):
    """Parse the arXiv Atom XML response and extract entries."""
    root = ET.fromstring(xml_content)
    entries = []
    for entry in root.findall('atom:entry', NS):
        title = entry.find('atom:title', NS).text.strip().replace('\n', ' ')
        
        # Extract the arXiv ID from the <id> tag (usually a URL)
        arxiv_id_full = entry.find('atom:id', NS).text
        arxiv_id = arxiv_id_full.split('/')[-1]

        # Find the PDF link (keep it as fallback or for PDF mode)
        pdf_link = None
        for link in entry.findall('atom:link', NS):
            if link.attrib.get('title') == 'pdf' or link.attrib.get('type') == 'application/pdf':
                pdf_link = link.attrib.get('href')
                break
        
        entries.append({
            'title': title,
            'id': arxiv_id,
            'pdf_link': pdf_link,
            'published': entry.find('atom:published', NS).text
        })
    return entries

def run_crawler(keywords, start_date, end_date, output_dir, max_results, download_format):
    """Main execution logic for the crawler."""
    # Ensure output directory exists
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Created output directory: {output_dir}")

    # Build search query
    query_parts = []
    for kw in keywords:
        # Wrap keyword in quotes if it contains spaces
        if ' ' in kw:
            query_parts.append(f'all:"{kw}"')
        else:
            query_parts.append(f'all:{kw}')
    
    search_query = " AND ".join(query_parts)
    
    # Handle dates
    if start_date or end_date:
        # arXiv date format: YYYYMMDDHHMM
        start_str = start_date.replace('-', '') + "0000" if start_date else "199108140000" # arXiv start
        end_str = end_date.replace('-', '') + "2359" if end_date else datetime.now().strftime("%Y%m%d%H%M")
        search_query += f" AND submittedDate:[{start_str} TO {end_str}]"

    base_url = "http://export.arxiv.org/api/query?"
    params = {
        'search_query': search_query,
        'start': 0,
        'max_results': max_results,
        'sortBy': 'submittedDate',
        'sortOrder': 'descending'
    }
    
    # URL encode correctly
    full_url = base_url + urllib.parse.urlencode(params)
    print(f"Querying arXiv: {full_url}")

    try:
        with urllib.request.urlopen(full_url) as response:
            xml_content = response.read()
    except Exception as e:
        print(f"Error querying arXiv API: {e}", file=sys.stderr)
        return

    entries = parse_arxiv_xml(xml_content)
    print(f"Found {len(entries)} matching articles.")

    for i, entry in enumerate(entries):
        title = entry['title']
        arxiv_id = entry['id']
        
        if download_format == 'pdf':
            download_url = entry['pdf_link']
            if not download_url.endswith('.pdf'):
                download_url += '.pdf'
            extension = '.pdf'
        else:
            # LaTeX source
            download_url = f"https://arxiv.org/src/{arxiv_id}"
            extension = '.tar.gz'

        safe_title = sanitize_filename(title)
        filename = f"{safe_title}{extension}"
        filepath = os.path.join(output_dir, filename)

        success = download_file(download_url, filepath)
        
        if success and i < len(entries) - 1:
            print(f"Waiting 12 seconds to respect rate limit (5 papers/min)...")
            time.sleep(12)

def main():
    parser = argparse.ArgumentParser(description="Arxiv Crawler: Automate research paper downloads.")
    parser.add_argument("--keywords", nargs="+", required=True, help="Keywords to search for.")
    parser.add_argument("--start-date", help="Start date in YYYY-MM-DD format.")
    parser.add_argument("--end-date", help="End date in YYYY-MM-DD format.")
    parser.add_argument("--output", default="./downloads", help="Output directory (default: ./downloads).")
    parser.add_argument("--max-results", type=int, default=5, help="Maximum number of results to download (default: 5).")
    parser.add_argument("--format", choices=['pdf', 'latex'], default='pdf', help="Download format: pdf or latex (default: pdf).")

    args = parser.parse_args()

    try:
        run_crawler(args.keywords, args.start_date, args.end_date, args.output, args.max_results, args.format)
    except KeyboardInterrupt:
        print("\nOperation cancelled by user.")
    except Exception as e:
        print(f"An unexpected error occurred: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
