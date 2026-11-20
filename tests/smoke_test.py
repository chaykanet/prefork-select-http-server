"""Функциональные проверки сервера; нагрузочные измерения сюда не входят."""
import concurrent.futures
import hashlib
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time


def request(port, path='/', method='GET', fragments=False):
    """Отправляет запрос и читает ответ до закрытия соединения."""
    with socket.create_connection(('127.0.0.1', port), timeout=10) as client:
        raw = f'{method} {path} HTTP/1.1\r\nHost: localhost\r\n\r\n'.encode()
        if fragments:
            client.sendall(raw[:8])
            time.sleep(.05)
            client.sendall(raw[8:])
        else:
            client.sendall(raw)
        data = bytearray()
        while True:
            block = client.recv(65536)
            if not block:
                break
            data.extend(block)
    headers, body = bytes(data).split(b'\r\n\r\n', 1)
    return int(headers.split()[1]), headers, body


def wait_for(predicate):
    """Ожидает проверяемое событие не более пяти секунд."""
    end = time.monotonic() + 5
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.05)
    raise AssertionError('event timeout')


def run(binary):
    """Проверяет HTTP, потоковую передачу, восстановление worker и остановку."""
    with tempfile.TemporaryDirectory(prefix='cn-http-') as folder:
        base = Path(folder)
        root = base / 'public'
        root.mkdir()
        (root / 'index.html').write_bytes(b'<h1>Test</h1>')
        (root / 'small.bin').write_bytes(os.urandom(8192))
        (root / 'style.css').write_text('body {color: navy}')
        (base / 'secret').write_text('secret')
        (root / 'link').symlink_to(base / 'secret')
        (root / 'outside').symlink_to(base, target_is_directory=True)
        (root / 'denied').write_text('denied')
        (root / 'denied').chmod(0)
        block = os.urandom(1024 * 1024)
        digest = hashlib.sha256()
        with (root / 'large.bin').open('wb') as file:
            for _ in range(128):
                file.write(block)
                digest.update(block)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        log = base / 'server.log'
        command = [binary, '--port', str(port), '--root', str(root), '--workers', '1',
                   '--timeout', '2', '--log', str(log)]
        process = subprocess.Popen(command, stderr=subprocess.PIPE, text=True)
        workers = set()
        try:
            wait_for(lambda: log.exists() and 'worker started' in log.read_text())
            workers = set(map(int, re.findall(r'pid=(\d+) worker started', log.read_text())))
            assert len(workers) == 1
            status, headers, body = request(port)
            assert status == 200 and body == (root / 'index.html').read_bytes()
            status, headers, body = request(port, method='HEAD')
            assert status == 200 and body == b'' and b'Content-Length: 13' in headers
            status, headers, _ = request(port, method='POST')
            assert status == 405 and b'Allow: GET, HEAD' in headers
            assert request(port, '/missing')[0] == 404
            for path in ['/../secret', '/%2e%2e/secret', '/link', '/outside/secret', '/.git/config', '/denied']:
                assert request(port, path)[0] == 403, path
                assert request(port, path, method='HEAD')[2] == b''
            assert request(port, '/%QQ')[0] == 400
            assert request(port, fragments=True)[0] == 200
            assert b'text/css' in request(port, '/style.css')[1]
            assert request(port, '/small.bin?download=1')[2] == (root / 'small.bin').read_bytes()
            status, headers, body = request(port, '/large.bin')
            assert status == 200 and len(body) == 128 * 1024 * 1024
            assert hashlib.sha256(body).digest() == digest.digest()
            del body
            with socket.create_connection(('127.0.0.1', port), timeout=5) as slow:
                slow.sendall(b'GET / HTTP/1.1\r\n')
                with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
                    assert all(result[0] == 200 for result in pool.map(lambda _: request(port), range(24)))
                assert slow.recv(1) == b''
            with socket.create_connection(('127.0.0.1', port), timeout=5) as large_header:
                large_header.sendall(b'GET / HTTP/1.1\r\nHost: x\r\nX: ' + b'a' * 17000)
                assert b'400 Bad Request' in large_header.recv(4096)
            with socket.create_connection(('127.0.0.1', port), timeout=5) as disconnected:
                disconnected.sendall(b'GET /large.bin HTTP/1.1\r\nHost: x\r\n\r\n')
            assert request(port)[0] == 200
            collision = subprocess.run(command, capture_output=True, timeout=5)
            assert collision.returncode != 0
            os.kill(next(iter(workers)), signal.SIGKILL)
            wait_for(lambda: log.read_text().count('worker started') == 2)
            workers |= set(map(int, re.findall(r'pid=(\d+) worker started', log.read_text())))
            assert request(port)[0] == 200
            process.send_signal(signal.SIGTERM)
            assert process.wait(timeout=5) == 0
            for pid in workers:
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    continue
                raise AssertionError(f'worker {pid} survived')
            process = subprocess.Popen(command, stderr=subprocess.PIPE, text=True)
            wait_for(lambda: log.read_text().count('worker started') == 3)
            process.send_signal(signal.SIGINT)
            assert process.wait(timeout=5) == 0
            print('PASS: HTTP, paths, HEAD, fragments, limits, timeout, 128 MiB SHA-256, concurrent clients, restart, signals')
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            (root / 'denied').chmod(0o600)


if __name__ == '__main__':
    run(str(Path(sys.argv[1]).resolve()))
