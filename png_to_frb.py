#!/usr/bin/env python3
"""
Convert numbered PNG/BMP frames to raw little-endian RGB565 .frb files.

Input:
    0.png, 1.png, 2.png ...
    or
    0.bmp, 1.bmp, 2.bmp ...

Output:
    0.frb, 1.frb, 2.frb ...

Format:
    Raw RGB565 frames.
    Row-major order.
    Each pixel is 16-bit RGB565, little-endian.
"""

from pathlib import Path
import argparse
import sys

from PIL import Image


SUPPORTED_EXTS = {".png", ".bmp"}


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    """
    Convert 8-bit RGB to RGB565.

    RGB565 layout:
        bits 15..11: red   5 bits
        bits 10..5 : green 6 bits
        bits 4..0  : blue  5 bits
    """
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def parse_bg(value: str) -> tuple[int, int, int]:
    """
    Parse background color for transparent PNGs.
    Accepts:
        black
        white
        #RRGGBB
        R,G,B
    """
    value = value.strip().lower()

    if value == "black":
        return 0, 0, 0
    if value == "white":
        return 255, 255, 255

    if value.startswith("#") and len(value) == 7:
        return (
            int(value[1:3], 16),
            int(value[3:5], 16),
            int(value[5:7], 16),
        )

    if "," in value:
        parts = value.split(",")
        if len(parts) != 3:
            raise ValueError(f"Invalid RGB background: {value}")

        rgb = tuple(int(p.strip()) for p in parts)
        if any(c < 0 or c > 255 for c in rgb):
            raise ValueError(f"RGB background values must be 0..255: {value}")

        return rgb

    raise ValueError(f"Invalid background color: {value}")


def load_image_rgb(path: Path, bg: tuple[int, int, int]) -> Image.Image:
    """
    Load image and return RGB image.

    If image has alpha, composite it over the selected background,
    because raw RGB565 has no alpha channel.
    """
    img = Image.open(path)

    if img.mode in ("RGBA", "LA") or ("transparency" in img.info):
        rgba = img.convert("RGBA")
        background = Image.new("RGBA", rgba.size, (*bg, 255))
        composed = Image.alpha_composite(background, rgba)
        return composed.convert("RGB")

    return img.convert("RGB")


def convert_one(
    input_path: Path,
    output_path: Path,
    width: int | None,
    height: int | None,
    bg: tuple[int, int, int],
    overwrite: bool,
) -> None:
    if output_path.exists() and not overwrite:
        raise FileExistsError(
            f"{output_path.name} already exists. Use --overwrite to replace it."
        )

    img = load_image_rgb(input_path, bg)

    if width is not None and height is not None:
        if img.size != (width, height):
            raise ValueError(
                f"{input_path.name}: expected {width}x{height}, got {img.size[0]}x{img.size[1]}"
            )

    out = bytearray()

    # Row-major order: y first, then x.
    for r, g, b in img.getdata():
        value = rgb888_to_rgb565(r, g, b)

        # little-endian uint16
        out.append(value & 0xFF)
        out.append((value >> 8) & 0xFF)

    output_path.write_bytes(out)

    print(f"{input_path.name} -> {output_path.name} ({len(out)} bytes)")


def numeric_stem(path: Path) -> int | None:
    try:
        return int(path.stem)
    except ValueError:
        return None


def find_numbered_images(folder: Path) -> list[Path]:
    images = []

    for path in folder.iterdir():
        if not path.is_file():
            continue

        if path.suffix.lower() not in SUPPORTED_EXTS:
            continue

        if numeric_stem(path) is None:
            continue

        images.append(path)

    images.sort(key=lambda p: numeric_stem(p))
    return images


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert numbered PNG/BMP frames to little-endian RGB565 .frb files."
    )

    parser.add_argument(
        "folder",
        nargs="?",
        default=".",
        help="Folder containing 0.png, 1.png, ... or 0.bmp, 1.bmp, ...",
    )

    parser.add_argument(
        "--width",
        type=int,
        default=64,
        help="Expected image width. Default: 64. Use 0 to disable size check.",
    )

    parser.add_argument(
        "--height",
        type=int,
        default=28,
        help="Expected image height. Default: 28. Use 0 to disable size check.",
    )

    parser.add_argument(
        "--bg",
        default="black",
        help="Background for transparent PNGs: black, white, #RRGGBB, or R,G,B. Default: black.",
    )

    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Overwrite existing .frb files.",
    )

    args = parser.parse_args()

    folder = Path(args.folder)

    if not folder.exists() or not folder.is_dir():
        print(f"Error: folder does not exist: {folder}", file=sys.stderr)
        return 1

    width = None if args.width == 0 else args.width
    height = None if args.height == 0 else args.height

    if (width is None) != (height is None):
        print("Error: width and height must both be set, or both be 0.", file=sys.stderr)
        return 1

    try:
        bg = parse_bg(args.bg)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    images = find_numbered_images(folder)

    if not images:
        print("No numbered .png/.bmp files found.", file=sys.stderr)
        return 1

    seen_stems: dict[str, Path] = {}

    for image_path in images:
        if image_path.stem in seen_stems:
            print(
                f"Error: both {seen_stems[image_path.stem].name} and {image_path.name} exist. "
                f"Only one source image per frame number is allowed.",
                file=sys.stderr,
            )
            return 1

        seen_stems[image_path.stem] = image_path

    try:
        for image_path in images:
            output_path = image_path.with_suffix(".frb")
            convert_one(
                input_path=image_path,
                output_path=output_path,
                width=width,
                height=height,
                bg=bg,
                overwrite=args.overwrite,
            )

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
