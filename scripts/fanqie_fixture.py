#!/usr/bin/env python3
"""Build a TF-card fixture for the X4 Pro Fanqie connector.

The book text is original fixture copy, not a published novel. The binary
records match FanqieStore version 1. Output is a directory that can be zipped
while preserving relative paths.
"""

import json
import struct
import zipfile
from pathlib import Path

BOOK_ID = "7000000000000000001"
EPUB_PATH = f"/Fanqie/{BOOK_ID}.epub"
TITLE = "Fanqie Fixture"
AUTHOR = "CrossMux"
CHAPTERS = [
    ("100", "Cached chapter", "This cached Fanqie chapter is fixture text.\n\nTurn the page to see the second paragraph stay in the existing reader."),
    ("101", "Unread chapter", ""),
]


def pack_meta() -> bytes:
    meta = struct.pack(
        "<24s96s64s96sI24sIIB1xHHH",
        BOOK_ID.encode(),
        TITLE.encode(),
        AUTHOR.encode(),
        EPUB_PATH.encode(),
        len(CHAPTERS),
        b"100",
        20,
        10,
        0,
        0,
        0,
        1,
    )
    assert len(meta) == 324
    header = struct.pack("<IHH", 0x4D425146, 1, 324)
    return header + meta


def pack_index() -> bytes:
    header = struct.pack("<IHH", 0x58495146, 1, 1)
    record = struct.pack("<24s96s", BOOK_ID.encode(), EPUB_PATH.encode())
    return header + record


def pack_chapters() -> bytes:
    header = struct.pack("<IHHI", 0x48435146, 1, 104, len(CHAPTERS))
    body = b"".join(struct.pack("<24s80s", item.encode(), title.encode()) for item, title, _ in CHAPTERS)
    return header + body


def epub_bytes() -> bytes:
    import io

    def store(name: str, data: bytes, entries: list) -> None:
        entries.append((name, data))

    chapters = []
    manifest = []
    spine = []
    for index, (item, title, text) in enumerate(CHAPTERS):
        body = text if text else "This chapter is not cached on the device yet."
        page = (
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>'
            f"{title}</title></head><body><h1>{title}</h1><p>{body}</p></body></html>"
        ).encode()
        chapters.append((f"OEBPS/chapter{index}.xhtml", page))
        manifest.append(
            f'<item id="c{index}" href="chapter{index}.xhtml" media-type="application/xhtml+xml"/>'
        )
        spine.append(f'<itemref idref="c{index}"/>')
    opf = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="bid" version="2.0">'
        '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
        f"<dc:title>{TITLE}</dc:title><dc:creator>{AUTHOR}</dc:creator>"
        "<dc:language>zh</dc:language>"
        f'<dc:identifier id="bid">fanqie-{BOOK_ID}</dc:identifier></metadata><manifest>'
        + "".join(manifest)
        + "</manifest><spine>"
        + "".join(spine)
        + "</spine></package>"
    ).encode()
    files = [
        ("mimetype", b"application/epub+zip"),
        (
            "META-INF/container.xml",
            b'<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
            b'<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
            b"</rootfiles></container>",
        ),
        ("OEBPS/content.opf", opf),
        *chapters,
    ]
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        for name, data in files:
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_STORED
            info.date_time = (2026, 1, 1, 0, 0, 0)
            archive.writestr(info, data)
    return out.getvalue()


def write_tree(root: Path) -> None:
    cross = root / ".crosspoint"
    book = cross / "fanqie" / "books" / BOOK_ID
    (book / "c").mkdir(parents=True, exist_ok=True)
    (root / "Fanqie").mkdir(exist_ok=True)
    (cross / "settings.json").write_text(json.dumps({"onboardingVersion": 1}), encoding="utf-8")
    (cross / "recent.json").write_text(
        json.dumps(
            {"books": [{"path": EPUB_PATH, "title": TITLE, "author": AUTHOR, "coverBmpPath": ""}]}
        ),
        encoding="utf-8",
    )
    (cross / "reading_stats.json").write_text(
        json.dumps(
            {
                "formatVersion": 6,
                "books": [
                    {
                        "bookId": "fanqie-fixture",
                        "path": EPUB_PATH,
                        "knownPaths": [EPUB_PATH],
                        "title": TITLE,
                        "author": AUTHOR,
                        "lastProgressPercent": 40,
                        "chapterProgressPercent": 10,
                        "chapterTitle": "Cached chapter",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    (cross / "fanqie" / "index.bin").write_bytes(pack_index())
    (book / "meta.bin").write_bytes(pack_meta())
    (book / "chapters.bin").write_bytes(pack_chapters())
    (book / "c" / "00000.txt").write_text(CHAPTERS[0][2], encoding="utf-8")
    (root / "Fanqie" / f"{BOOK_ID}.epub").write_bytes(epub_bytes())


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.suffix == ".zip":
        tree = args.output.with_suffix("")
        write_tree(tree)
        with zipfile.ZipFile(args.output, "w") as archive:
            for path in tree.rglob("*"):
                if path.is_file():
                    archive.write(path, path.relative_to(tree).as_posix())
    else:
        write_tree(args.output)


if __name__ == "__main__":
    main()
