"""Headless rendering checks for the local textured schematic viewer.

Requires Playwright plus its installed Chromium. Uses only
generated test textures. Does not connect to Minecraft or change any world.
"""
import argparse
import json
import re
from pathlib import Path
import struct
import tempfile
import zlib

from playwright.sync_api import sync_playwright


def png(path, color):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(color) * 16 for _ in range(16))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--html', type=Path, help='Also inspect a real #preview3d export')
    parser.add_argument('--browser', type=Path, help='Optional existing Chromium/Chrome executable')
    parser.add_argument('--output', type=Path, default=Path('re/preview3d-validation.png'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='baritone-preview-') as temp:
        base = Path(temp)
        pack = base / 'pack'
        (pack / 'textures/blocks').mkdir(parents=True)
        (pack / 'blocks.json').write_text(json.dumps({'stone': {'textures': 'stone'}, 'oak_log': {'textures': {'up': 'log_top', 'down': 'log_top', 'side': 'log_side'}}}))
        (pack / 'textures/terrain_texture.json').write_text('// JSONC comment\n' + json.dumps({'texture_data': {name: {'textures': 'textures/blocks/' + name} for name in ['stone', 'log_top', 'log_side']}}))
        for name, color in [('stone', (125, 145, 175)), ('log_top', (60, 190, 85)), ('log_side', (175, 100, 45))]:
            png(pack / f'textures/blocks/{name}.png', color)
        fixture = {'format': 1, 'name': 'Render regression fixture', 'origin': [0, 64, 0], 'size': [2, 2, 1], 'blocks': [
            {'p': [0, 0, 0], 'name': 'minecraft:stone', 'states': {}},
            {'p': [1, 0, 0], 'name': 'minecraft:oak_log', 'states': {'pillar_axis': 'x'}},
            {'p': [0, 1, 0], 'name': 'minecraft:oak_log', 'states': {'pillar_axis': 'z'}},
        ]}
        html = (root / 'apps/schematic-viewer.html').read_text(encoding='utf-8').replace('/*BARITONE_SCHEMATIC*/null', json.dumps(fixture))
        file = base / 'fixture.html'; file.write_text(html, encoding='utf-8')
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=str(args.browser) if args.browser else None, headless=True, args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
            page = browser.new_page(viewport={'width': 1200, 'height': 800})
            errors, network = [], []
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.on('request', lambda request: network.append(request.url) if request.url.startswith(('http:', 'https:')) else None)
            page.goto(file.as_uri()); page.locator('#status').filter(has_text='visible faces').wait_for()
            assert page.evaluate('previewDiagnostics.vertices') > 0
            page.locator('#pack').set_input_files(str(pack))
            page.locator('#status').filter(has_text='3 loaded textures').wait_for()
            assert page.evaluate('previewDiagnostics.missing') == 0
            full = page.evaluate('previewDiagnostics.vertices')
            screenshot = page.screenshot()
            page.locator('#high').evaluate('(e)=>{e.value=0;e.dispatchEvent(new Event("input"));}')
            assert 0 < page.evaluate('previewDiagnostics.vertices') < full
            page.locator('#opacity').evaluate('(e)=>{e.value=40;e.dispatchEvent(new Event("input"));}')
            page.wait_for_timeout(100)
            assert page.screenshot() != screenshot
            page.locator('#canvas').hover(); page.mouse.move(700, 400); page.mouse.down(); page.mouse.move(820, 450, steps=5); page.mouse.up()
            args.output.parent.mkdir(parents=True, exist_ok=True)
            page.screenshot(path=str(args.output))
            if args.html:
                page.goto(args.html.resolve().as_uri()); page.locator('#status').filter(has_text='visible faces').wait_for()
                assert page.evaluate('previewDiagnostics.vertices') > 0, 'Native export contains no renderable geometry'
                page.locator('#pack').set_input_files(str(pack)); page.locator('#status').filter(has_text=re.compile(r'[1-9]\d* loaded textures')).wait_for()
            assert not errors and not network, {'errors': errors, 'unexpected_network': network}
            assert not page.evaluate('previewDiagnostics.errors'), page.evaluate('previewDiagnostics.errors')
            browser.close()
    print('PASS textured WebGL rendering, local pack import, JSONC, layers, opacity, orbit and no network requests')
    print(f'Render: {args.output.resolve()}')


if __name__ == '__main__':
    main()
