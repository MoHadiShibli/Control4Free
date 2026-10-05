"""All filesystem/device substitutions stay in this host-only test binary."""
import subprocess


def main():
    flags = ['clang-18', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-Itests/include']
    subprocess.run(flags + ['-ffunction-sections', '-fdata-sections', 'src/vda.c', 'tests/klog_test.c',
                           '-Wl,--gc-sections,--wrap=open', '-pthread', '-o', 'build/klog-host-test'], check=True)
    for mode in ('direct', 'busy'):
        subprocess.run(['build/klog-host-test', mode], check=True, timeout=8)
    subprocess.run(flags + ['src/log.c', 'tests/log_test.c',
                           '-Wl,--wrap=clock_gettime', '-o', 'build/log-host-test'], check=True)
    subprocess.run(['build/log-host-test'], check=True, timeout=5)
    subprocess.run(flags + ['src/klog_line.c', 'tests/device_id_test.c',
                           '-o', 'build/device-id-host-test'], check=True)
    subprocess.run(['build/device-id-host-test'], check=True, timeout=5)
    subprocess.run(flags + ['src/feedback.c', 'tests/feedback_test.c', '-o', 'build/feedback-host-test'], check=True)
    subprocess.run(['build/feedback-host-test'], check=True, timeout=5)


if __name__ == '__main__':
    main()
