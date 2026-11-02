"""用文件握手验证子进程未结束时日志已可见，避免依赖机器快慢的固定 sleep 断言。"""
import importlib.util
from pathlib import Path
import sys
import threading
import time

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('driver', sys.argv[1])
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)
root = Path(sys.argv[2])
root.mkdir(parents=True, exist_ok=True)
release = root / 'release'
code = ('import pathlib,sys,time; print("live-marker", flush=True); '
        'p=pathlib.Path(sys.argv[1]);\nwhile not p.exists(): time.sleep(0.01)')
result = []
worker = threading.Thread(target=lambda: result.append(driver.run(
    [sys.executable, '-c', code, str(release)], str(root), 'solve')))
worker.start()
visible = False
try:
    deadline = time.monotonic() + 10
    log = root / 'logs/solve.log'
    while worker.is_alive() and time.monotonic() < deadline:
        if log.exists() and '\nlive-marker\n' in log.read_text():
            visible = True
            break
        time.sleep(0.01)
finally:
    release.touch()
    worker.join(10)
assert visible, 'the process output was buffered until exit'
assert not worker.is_alive() and result == [0]
