"""Рисует простой SVG-график индивидуальных скоростей без внешних библиотек."""
import csv
import html
from pathlib import Path
import sys


def main():
    """Читает connections.csv и сохраняет speeds.svg рядом с исходными данными."""
    source = Path(sys.argv[1])
    with source.open() as file:
        rows = list(csv.DictReader(file))
    if not rows:
        raise SystemExit('В CSV нет данных.')
    maximum = max(float(row['MiB_per_s']) for row in rows) or 1
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="500" viewBox="0 0 1000 500">',
             '<rect width="1000" height="500" fill="white"/>',
             '<g font-family="sans-serif" font-size="16" fill="#182c40">',
             '<text x="500" y="30" text-anchor="middle">Скорость каждого соединения</text>',
             '<text x="500" y="480" text-anchor="middle">Номер соединения</text>',
             '<text x="20" y="250" transform="rotate(-90 20 250)" text-anchor="middle">Скорость, МиБ/с</text>',
             '<text x="80" y="55">Синий — успешно, красный — ошибка</text>']
    for i in range(6):
        y = 420 - i * 68
        parts.append(f'<path d="M80 {y} H960" stroke="#ddd"/><text x="70" y="{y + 5}" text-anchor="end">{maximum * i / 5:.1f}</text>')
    width = 880 / len(rows)
    for i, row in enumerate(rows):
        height = float(row['MiB_per_s']) / maximum * 340
        color = '#2878b8' if row['status'] == 'ok' else '#cc3333'
        title = html.escape(f"Соединение {row['connection_id']}: {row['MiB_per_s']} МиБ/с, {row['status']}")
        parts.append(f'<rect x="{80 + i * width}" y="{420 - max(height, 1)}" width="{max(width * .8, .5)}" height="{max(height, 1)}" fill="{color}"><title>{title}</title></rect>')
        if i % max(1, len(rows) // 10) == 0:
            parts.append(f'<text x="{80 + i * width}" y="445">{html.escape(row["connection_id"])}</text>')
    parts.append('</g></svg>')
    target = source.with_name('speeds.svg')
    target.write_text('\n'.join(parts))
    print(target)


if __name__ == '__main__':
    main()
