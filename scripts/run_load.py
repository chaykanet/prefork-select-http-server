"""Запускает один нагрузочный сценарий и сохраняет читаемые результаты."""
import argparse
from datetime import datetime
from pathlib import Path
import platform
import resource
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def wait_ready(server, port):
    """Ожидает готовность сервера, проверяя также завершение процесса."""
    for _ in range(100):
        if server.poll() is not None:
            raise RuntimeError('Сервер не запустился. См. server-stderr.txt.')
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=.1):
                return
        except OSError:
            time.sleep(.05)
    raise RuntimeError('Сервер не ответил за 5 секунд.')


def main():
    """Готовит файл, запускает сервер и выбранный измеритель, останавливает сервер."""
    parser = argparse.ArgumentParser()
    parser.add_argument('scenario', choices=['1', '2'])
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--connections', type=int, default=100)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--size-mib', type=int, default=16)
    parser.add_argument('--port', type=int, default=8081)
    args = parser.parse_args()

    if not (1 <= args.workers <= 256 and 1 <= args.connections <= 1000 and
            1 <= args.seconds <= 3600 and 1 <= args.size_mib <= 128 and 1 <= args.port <= 65535):
        parser.error('Недопустимые параметры: workers 1–256, connections 1–1000, seconds 1–3600, size-mib 1–128, port 1–65535.')

    if args.scenario == '1' and not shutil.which('wrk'):
        parser.error('Не найдена утилита wrk.')

    with socket.socket() as probe:
        probe.bind(('127.0.0.1', args.port))

    stamp = datetime.now().strftime('%Y-%m-%d_%H-%M-%S_%f')
    output = ROOT / 'artifacts' / f'load{args.scenario}_{stamp}'
    output.mkdir(parents=True)
    print(f'Результаты: {output}', flush=True)
    info = (f'Сценарий: {args.scenario}\nWorkers: {args.workers}\nСоединения: {args.connections}\n'
            f'Размер файла, МиБ: {args.size_mib}\nДлительность wrk, с: {args.seconds}\n'
            f'Система: {platform.platform()}\nАрхитектура: {platform.machine()}\n'
            f'Лимиты дескрипторов: {resource.getrlimit(resource.RLIMIT_NOFILE)}\n'
            'Клиент и сервер: localhost\nConnection: close\nФайл может находиться в кэше ОС.\n')
    (output / 'settings.txt').write_text(info)

    with tempfile.TemporaryDirectory(prefix='cn-load-') as folder:
        payload = Path(folder) / 'payload.bin'
        with payload.open('wb') as file:
            block = bytes(range(256)) * 4096
            for _ in range(args.size_mib):
                file.write(block)
        command = [str(ROOT / 'build/static_server'), '--root', folder,
                   '--port', str(args.port), '--workers', str(args.workers),
                   '--max-connections', str(min(args.connections, 900)),
                   '--timeout', '60', '--log', str(output / 'server.log')]
        with (output / 'server-stderr.txt').open('w') as errors:
            server = subprocess.Popen(command, stderr=errors)
            try:
                wait_ready(server, args.port)

                if args.scenario == '1':
                    command = ['wrk', f'-t{min(4, args.connections)}', f'-c{args.connections}',
                               f'-d{args.seconds}s', '--latency',
                               f'http://127.0.0.1:{args.port}/payload.bin']
                else:
                    command = [str(ROOT / 'build/connection_test'), str(args.port),
                               str(args.connections), str(output / 'connections.csv')]
                (output / 'command.txt').write_text(' '.join(command) + '\n')
                with (output / 'result.txt').open('w') as result:
                    completed = subprocess.run(command, stdout=result, stderr=subprocess.STDOUT,
                                               timeout=args.seconds + 120 if args.scenario == '1' else 180)
                print((output / 'result.txt').read_text())

                if completed.returncode:
                    raise RuntimeError('Измеритель завершился с ошибкой. См. result.txt и connections.csv.')
            finally:
                server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f'Ошибка: {error}')
