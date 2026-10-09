#!/usr/bin/env python3
"""Extract release notes and make repository-relative Markdown links tag-specific."""
import re
import sys
from pathlib import Path
from urllib.parse import urljoin, urlsplit


def release_notes(changelog, version, repository):
    lines = changelog.splitlines(keepends=True)
    active = False
    notes = []
    for line in lines:
        if line.startswith('## '):
            if active:
                break
            active = line.startswith(f'## {version} ')
            continue
        if active:
            notes.append(line)
    text = ''.join(notes).strip()
    if not text:
        raise ValueError(f'CHANGELOG.md has no notes for {version}')
    base = f'https://github.com/{repository}/blob/v{version}/'

    def absolute(match):
        label, target = match.groups()
        if target.startswith('#') or urlsplit(target).scheme or target.startswith('//'):
            return match.group(0)
        return f'[{label}]({urljoin(base, target)})'

    return re.sub(r'\[([^\]]+)\]\(([^\s)]+)\)', absolute, text) + '\n'


if __name__ == '__main__':
    path, version, repository = sys.argv[1:]
    print(release_notes(Path(path).read_text(), version, repository), end='')
