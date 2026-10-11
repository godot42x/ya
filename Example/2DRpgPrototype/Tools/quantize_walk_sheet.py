"""Turn a generated walk sheet into a pixel-art sheet with 16px wide cells.

Input: 12 figures on a flat key colour, laid out as 3 columns (left foot,
stand, right foot) x 4 rows (down, left, right, up). Generated sheets do not
sit on an exact grid, so figures are found as connected regions and ordered by
position. Output: RGBA, one CELL_W x cell-height cell per frame, feet on the
cell's bottom row, one shared palette. The right row is the left row mirrored,
so the two always agree.

    python3 quantize_walk_sheet.py raw.png out.png [--cell-height 24] [--preview preview.png]
"""

import argparse
from collections import Counter, deque

from PIL import Image

COLUMNS, ROWS, CELL_W = 3, 4, 16
ROW_LEFT, ROW_RIGHT = 1, 2
KEY_DISTANCE = 90  # distance from the key colour beyond which a pixel is figure
PALETTE_SIZE = 12
MAJORITY = 0.5  # share of figure pixels a source block needs to become opaque


def figure_mask(image, key):
    width, height = image.size
    pixels = image.load()
    limit = KEY_DISTANCE**2
    return [
        [sum((a - b) ** 2 for a, b in zip(pixels[x, y], key)) > limit for x in range(width)] for y in range(height)
    ]


def regions(mask):
    """8-connected figure regions as (area, left, top, right, bottom, cx, cy)."""
    height, width = len(mask), len(mask[0])
    seen = [[False] * width for _ in range(height)]
    found = []
    for sy in range(height):
        for sx in range(width):
            if not mask[sy][sx] or seen[sy][sx]:
                continue
            seen[sy][sx] = True
            queue = deque([(sx, sy)])
            area, left, top, right, bottom, sum_x, sum_y = 0, sx, sy, sx, sy, 0, 0
            while queue:
                x, y = queue.popleft()
                area += 1
                sum_x, sum_y = sum_x + x, sum_y + y
                left, top, right, bottom = min(left, x), min(top, y), max(right, x), max(bottom, y)
                for ny in range(max(0, y - 1), min(height, y + 2)):
                    for nx in range(max(0, x - 1), min(width, x + 2)):
                        if mask[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            queue.append((nx, ny))
            found.append([area, left, top, right + 1, bottom + 1, sum_x / area, sum_y / area])
    return found


def figures(mask):
    """The 12 figure boxes in row-major order; stray specks join the nearest figure."""
    found = sorted(regions(mask), key=lambda r: -r[0])
    if len(found) < COLUMNS * ROWS:
        raise SystemExit(f"expected {COLUMNS * ROWS} figures, found {len(found)} regions")
    boxes = found[: COLUMNS * ROWS]
    for speck in found[COLUMNS * ROWS :]:
        owner = min(boxes, key=lambda b: (b[5] - speck[5]) ** 2 + (b[6] - speck[6]) ** 2)
        owner[1], owner[2] = min(owner[1], speck[1]), min(owner[2], speck[2])
        owner[3], owner[4] = max(owner[3], speck[3]), max(owner[4], speck[4])
    boxes.sort(key=lambda b: b[6])
    rows = [sorted(boxes[r * COLUMNS : (r + 1) * COLUMNS], key=lambda b: b[5]) for r in range(ROWS)]
    return [[tuple(b[1:5]) for b in row] for row in rows]


def downsample(indexed, mask, box, scale, size):
    """Majority palette index of each source block; transparent where figure is sparse."""
    left, top, right, bottom = box
    width, height = size
    pixels = indexed.load()
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    palette = indexed.getpalette()
    block = 1.0 / scale
    # Feet on the bottom row, figure centred horizontally.
    origin_x = (left + right) / 2 - width * block / 2
    origin_y = bottom - height * block
    for ty in range(height):
        for tx in range(width):
            x0, x1 = round(origin_x + tx * block), round(origin_x + (tx + 1) * block)
            y0, y1 = round(origin_y + ty * block), round(origin_y + (ty + 1) * block)
            votes = Counter()
            total = 0
            for y in range(max(y0, top), min(y1, bottom)):
                for x in range(max(x0, left), min(x1, right)):
                    total += 1
                    if mask[y][x]:
                        votes[pixels[x, y]] += 1
            cells = max(1, (x1 - x0) * (y1 - y0))
            if votes and sum(votes.values()) >= MAJORITY * cells:
                index = votes.most_common(1)[0][0]
                out.putpixel((tx, ty), tuple(palette[index * 3 : index * 3 + 3]) + (255,))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("raw")
    parser.add_argument("out")
    parser.add_argument("--cell-height", type=int, default=24)
    parser.add_argument("--preview")
    args = parser.parse_args()

    image = Image.open(args.raw).convert("RGB")
    key = image.getpixel((2, 2))
    mask = figure_mask(image, key)
    boxes = figures(mask)

    # One palette from figure pixels only: the key colour must not take a slot.
    figure_pixels = [image.getpixel((x, y)) for y in range(0, image.height, 2) for x in range(0, image.width, 2) if mask[y][x]]
    sample = Image.new("RGB", (len(figure_pixels), 1))
    sample.putdata(figure_pixels)
    palette_image = sample.quantize(PALETTE_SIZE, method=Image.Quantize.MEDIANCUT)
    indexed = image.quantize(palette=palette_image, dither=Image.Dither.NONE)

    cell_h = args.cell_height
    tallest = max(b[3] - b[1] for row in boxes for b in row)
    scale = cell_h / tallest
    size = (CELL_W, cell_h)

    sheet = Image.new("RGBA", (COLUMNS * CELL_W, ROWS * cell_h), (0, 0, 0, 0))
    for row in range(ROWS):
        source_row = ROW_LEFT if row == ROW_RIGHT else row
        for column in range(COLUMNS):
            frame = downsample(indexed, mask, boxes[source_row][column], scale, size)
            if row == ROW_RIGHT:
                frame = frame.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            sheet.paste(frame, (column * CELL_W, row * cell_h))

    sheet.save(args.out)
    if args.preview:
        sheet.resize((sheet.width * 8, sheet.height * 8), Image.Resampling.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
