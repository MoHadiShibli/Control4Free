"""Tag-specific release notes keep documentation links usable on GitHub Releases."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from release_notes import release_notes

text = '''# Changelog
## 2.3.4 — Unreleased
[Guide](docs/playing.md#gamepads) [External](https://example.com/) [Anchor](#top)
## 2.3.3 — Before
older notes
'''
result = release_notes(text, '2.3.4', 'example/project')
assert '[Guide](https://github.com/example/project/blob/v2.3.4/docs/playing.md#gamepads)' in result
assert '[External](https://example.com/)' in result and '[Anchor](#top)' in result
assert 'older notes' not in result
try:
    release_notes(text, 'missing', 'example/project')
except ValueError:
    pass
else:
    raise AssertionError('missing changelog version accepted')
print('PASS release-note extraction and tag-specific links')
