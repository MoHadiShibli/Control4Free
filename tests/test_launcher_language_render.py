"""Language changes must invalidate every text-bearing tester cache."""
import subprocess
from pathlib import Path
import test_tester_render


def main():
    binary = test_tester_render.build(
        output=Path('build/launcher/language-render-test').resolve(),
        fixture='tests/launcher_language_render_test.c', extra_sources=['launcher/language.c'])
    subprocess.run([str(binary), 'names'], check=True, timeout=30)
    for language in ('en', 'pt', 'ar'):
        for variant in range(6):
            fresh = subprocess.check_output([str(binary), language, 'fresh', str(variant)], timeout=60)
            warm = subprocess.check_output([str(binary), language, 'warm', str(variant)], timeout=60)
            assert fresh == warm, (language, variant, fresh, warm)
    print('PASS all RGB focus/precision variants match fresh rendering after both language-switch directions', flush=True)
    for language in ('pt', 'ar'):
        expected = []
        for variant in range(4):
            fresh = subprocess.check_output([str(binary), 'metadata', language, 'fresh', str(variant)], timeout=60)
            warm = subprocess.check_output([str(binary), 'metadata', language, 'warm', str(variant)], timeout=60)
            assert fresh == warm, ('metadata', language, variant, fresh, warm)
            expected.append(fresh)
        assert expected[0] != expected[1], ('generated name was not visibly distinct', language)
        assert expected[2] != expected[3], ('message argument provenance was not visibly distinct', language)
    print('PASS generated names and message provenance invalidate tools cache in both directions', flush=True)


if __name__ == '__main__':
    main()
