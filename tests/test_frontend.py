"""The page's own claim and Gamepad functions, run under Node."""
import subprocess


def main():
    subprocess.run(['node', 'tests/frontend_test.cjs'], check=True, timeout=30)
    subprocess.run(['node', 'tests/mapping_test.cjs'], check=True, timeout=30)
    subprocess.run(['python3', '-B', 'tests/release_notes_test.py'], check=True, timeout=10)


if __name__ == '__main__':
    main()
