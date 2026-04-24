import os
import pathlib
import re
import subprocess
import sys


def resolve_font() -> str:
    preferred = os.environ.get('LUNARIS_FONT', 'JetBrains Mono')
    if os.path.isfile(preferred):
        return preferred

    candidates = [preferred, 'JetBrains Mono', 'DejaVu Sans Mono']
    for family in candidates:
        p = subprocess.run(['fc-match', '-f', '%{file}\n', family], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if p.returncode != 0:
            continue
        resolved = p.stdout.strip().splitlines()[0] if p.stdout.strip() else ''
        if not resolved:
            continue
        if family == 'JetBrains Mono' and 'JetBrains' not in pathlib.Path(resolved).name:
            continue
        if os.path.isfile(resolved):
            return resolved

    fallbacks = [
        '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',
        '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    ]
    for fallback in fallbacks:
        if os.path.isfile(fallback):
            return fallback
    raise FileNotFoundError('could not resolve a monospace font')


font = resolve_font()
out = pathlib.Path('/mnt/d/Lunaris/LunarisOS/kernel/src/terminal_font.lua')
lines = []
font_words = []
glyph_width = 16
glyph_height = 32
rows_per_word = 4
font_word_index = 0
for ch in range(32, 127):
    glyph = chr(ch)
    cmd = [
        'convert', '-size', '128x256', 'xc:black',
        '-font', font, '-fill', 'white', '-pointsize', '160',
        '-gravity', 'center', '-annotate', '0', glyph,
        '-filter', 'Lanczos', '-resize', f'{glyph_width}x{glyph_height}!', '-threshold', '50%', 'txt:-'
    ]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if p.returncode != 0:
        sys.stderr.write(f'convert failed for U+{ch:02X} {glyph!r}\n')
        sys.stderr.write(p.stderr)
        sys.exit(p.returncode)
    rows = [0] * glyph_height
    for line in p.stdout.splitlines():
        m = re.match(r'^(\d+),(\d+): .*?\s(white|black)$', line)
        if not m:
            continue
        x = int(m.group(1)); y = int(m.group(2)); color = m.group(3)
        if color == 'white':
            rows[y] |= 1 << (glyph_width - 1 - x)
    for block in range(0, glyph_height, rows_per_word):
        word = 0
        for row_offset in range(rows_per_word):
            word |= rows[block + row_offset] << (16 * row_offset)
        font_words.append(word)
        lines.append(f'data terminal_font_word_{font_word_index}: u64 section ".rodata" = 0x{word:016X}')
        font_word_index += 1
lines.append('')
lines.append('function terminal_font_word(index: u64)')
for index in range(font_word_index):
    if index == 0:
        lines.append(f'    if index == {index} then')
    else:
        lines.append(f'    if index == {index} then')
    lines.append(f'        return terminal_font_word_{index}')
    lines.append('    end')
lines.append('    return 0')
lines.append('end')
out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(out)
