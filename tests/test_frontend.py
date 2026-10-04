"""The page's own claim and Gamepad functions, run under Node."""
import subprocess


def main():
    subprocess.run(['node', 'tests/frontend_test.cjs'], check=True, timeout=30)


if __name__ == '__main__':
    main()
