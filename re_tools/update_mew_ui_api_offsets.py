from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_pointer: int
    raw_size: int

@dataclass(frozen=True)
class MatchResult:
    symbol_name: str
    value: int
    section_name: str
    file_offset: int

class PeImage:
    def __init__(self, image_path: Path) -> None:
        self.image_path: Path = image_path
        self.data: bytes = image_path.read_bytes()
        self.sections: list[Section] = self._read_sections()

    def _read_sections(self) -> list[Section]:
        if len(self.data) < 0x40 or self.data[0:2] != b"MZ":
            raise ValueError("Input is not a valid PE file: missing MZ header.")

        pe_offset: int = struct.unpack_from("<I", self.data, 0x3C)[0]

        if self.data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("Input is not a valid PE file: missing PE signature.")

        section_count: int = struct.unpack_from("<H", self.data, pe_offset + 6)[0]
        optional_header_size: int = struct.unpack_from("<H", self.data, pe_offset + 20)[0]
        section_table_offset: int = pe_offset + 24 + optional_header_size
        sections: list[Section] = []

        for index in range(section_count):
            offset: int = section_table_offset + (index * 40)
            raw_name: bytes = self.data[offset:offset + 8]
            name: str = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
            virtual_size: int
            virtual_address: int
            raw_size: int
            raw_pointer: int
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", self.data, offset + 8)
            sections.append(Section(name, virtual_address, virtual_size, raw_pointer, raw_size))

        return sections

    def file_offset_to_rva(self, file_offset: int) -> int:
        for section in self.sections:
            mapped_size: int = max(section.virtual_size, section.raw_size)

            if section.raw_pointer <= file_offset < section.raw_pointer + mapped_size:
                return section.virtual_address + (file_offset - section.raw_pointer)

        raise ValueError(f"File offset 0x{file_offset:X} is not inside a PE section.")

    def rva_to_file_offset(self, rva: int) -> tuple[int, Section]:
        for section in self.sections:
            mapped_size: int = max(section.virtual_size, section.raw_size)

            if section.virtual_address <= rva < section.virtual_address + mapped_size:
                return section.raw_pointer + (rva - section.virtual_address), section

        raise ValueError(f"RVA 0x{rva:X} is not inside a PE section.")

    def get_section_bytes(self, section: Section) -> bytes:
        mapped_size: int = max(section.virtual_size, section.raw_size)
        return self.data[section.raw_pointer:min(len(self.data), section.raw_pointer + mapped_size)]

def parse_pattern(pattern_text: str) -> tuple[bytes, list[bool]]:
    values: list[int] = []
    mask: list[bool] = []

    for token in pattern_text.split():
        if token == "?" or token == "??":
            values.append(0)
            mask.append(False)
            continue

        if not re.fullmatch(r"[0-9A-Fa-f]{2}", token):
            raise ValueError(f"Invalid pattern token: {token}")

        values.append(int(token, 16))
        mask.append(True)

    return bytes(values), mask

def find_pattern(buffer: bytes, pattern: bytes, mask: list[bool]) -> list[int]:
    pattern_length: int = len(pattern)

    if pattern_length == 0:
        return []

    fixed_indices: list[int] = [index for index, is_fixed in enumerate(mask) if is_fixed]

    if not fixed_indices:
        return []

    anchor_index: int = fixed_indices[0]
    anchor_value: int = pattern[anchor_index]
    matches: list[int] = []
    search_from: int = 0

    while True:
        anchor_position: int = buffer.find(bytes([anchor_value]), search_from)

        if anchor_position < 0:
            break

        start: int = anchor_position - anchor_index

        if start >= 0 and start + pattern_length <= len(buffer):
            is_match: bool = True

            for index in fixed_indices:
                if buffer[start + index] != pattern[index]:
                    is_match = False
                    break

            if is_match:
                matches.append(start)

        search_from = anchor_position + 1

    return matches

def find_unique_pattern(pe_image: PeImage, symbol_name: str, symbol_config: dict[str, Any]) -> tuple[Section, int]:
    pattern_text: str = str(symbol_config["pattern"])
    pattern: bytes
    mask: list[bool]
    pattern, mask = parse_pattern(pattern_text)
    search_section_names: set[str] = set(str(value) for value in symbol_config.get("search_sections", []))
    found: list[tuple[Section, int]] = []

    for section in pe_image.sections:
        if search_section_names and section.name not in search_section_names:
            continue

        section_data: bytes = pe_image.get_section_bytes(section)

        for relative_offset in find_pattern(section_data, pattern, mask):
            found.append((section, relative_offset))

    if len(found) == 0:
        raise RuntimeError(f"{symbol_name}: signature did not match anything.")

    if len(found) > 1:
        locations: str = ", ".join(f"{section.name}+0x{relative_offset:X}" for section, relative_offset in found[:8])
        raise RuntimeError(f"{symbol_name}: signature matched {len(found)} locations: {locations}")

    return found[0]

def resolve_symbol(pe_image: PeImage, symbol_name: str, symbol_config: dict[str, Any]) -> MatchResult:
    resolver: str = str(symbol_config.get("resolver", "pattern_start_rva"))

    if resolver == "constant":
        value: int = int(symbol_config["value"])
        return MatchResult(symbol_name, value, "constant", 0)

    section: Section
    relative_offset: int
    section, relative_offset = find_unique_pattern(pe_image, symbol_name, symbol_config)

    if resolver == "pattern_start_rva":
        match_offset: int = int(symbol_config.get("match_offset", 0))
        file_offset: int = section.raw_pointer + relative_offset + match_offset
        value = pe_image.file_offset_to_rva(file_offset)
        return MatchResult(symbol_name, value, section.name, file_offset)

    if resolver == "rip_relative_rva":
        displacement_offset: int = int(symbol_config["displacement_offset"])
        instruction_end_offset: int = int(symbol_config["instruction_end_offset"])
        displacement_file_offset: int = section.raw_pointer + relative_offset + displacement_offset
        displacement: int = struct.unpack_from("<i", pe_image.data, displacement_file_offset)[0]
        instruction_end_rva: int = section.virtual_address + relative_offset + instruction_end_offset
        value = instruction_end_rva + displacement
        resolved_file_offset: int
        resolved_section: Section
        resolved_file_offset, resolved_section = pe_image.rva_to_file_offset(value)
        return MatchResult(symbol_name, value, resolved_section.name, displacement_file_offset)

    if resolver == "u8_at_pattern_offset":
        value_offset: int = int(symbol_config["value_offset"])
        file_offset = section.raw_pointer + relative_offset + value_offset
        value = pe_image.data[file_offset]
        return MatchResult(symbol_name, value, section.name, file_offset)

    if resolver == "u32_at_pattern_offset":
        value_offset = int(symbol_config["value_offset"])
        file_offset = section.raw_pointer + relative_offset + value_offset
        value = struct.unpack_from("<I", pe_image.data, file_offset)[0]
        return MatchResult(symbol_name, value, section.name, file_offset)

    raise ValueError(f"{symbol_name}: unknown resolver '{resolver}'.")

def format_define_value(original_numeric_text: str, value: int) -> str:
    if original_numeric_text.lower().startswith("0x"):
        width: int = max(len(original_numeric_text) - 2, 2)
        return f"0x{value:0{width}X}"

    return str(value)

def update_header_text(header_text: str, resolved_values: dict[str, int]) -> tuple[str, list[str]]:
    updated_symbols: list[str] = []

    for symbol_name, value in resolved_values.items():
        pattern: re.Pattern[str] = re.compile(rf"(^\s*#define\s+{re.escape(symbol_name)}\s+)(0x[0-9A-Fa-f]+|\d+)([uUlL]*)(.*$)", re.MULTILINE)

        def replace(match: re.Match[str]) -> str:
            original_numeric_text: str = match.group(2)
            suffix: str = match.group(3)
            updated_symbols.append(symbol_name)
            return f"{match.group(1)}{format_define_value(original_numeric_text, value)}{suffix}{match.group(4)}"

        header_text, replacement_count = pattern.subn(replace, header_text, count=1)

        if replacement_count != 1:
            raise RuntimeError(f"{symbol_name}: could not find matching #define in header.")

    return header_text, updated_symbols

def load_signatures(signature_path: Path) -> dict[str, Any]:
    with signature_path.open("r", encoding="utf-8") as handle:
        config: dict[str, Any] = json.load(handle)

    if "symbols" not in config or not isinstance(config["symbols"], dict):
        raise ValueError("Signature file must contain a 'symbols' object.")

    return config

def main() -> int:
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description="Update mew_ui_api.h RVAs and constants from Mewgenics byte signatures.")
    parser.add_argument("exe_path", type=Path, help="Path to the updated Mewgenics executable.")
    parser.add_argument("header_path", type=Path, help="Path to mew_ui_api.h.")
    parser.add_argument("--signatures", type=Path, default=Path("mew_ui_api_signatures.json"), help="Path to mew_ui_api_signatures.json.")
    parser.add_argument("--dry-run", action="store_true", help="Resolve symbols and print results without rewriting the header.")
    parser.add_argument("--no-backup", action="store_true", help="Do not create a .bak copy before rewriting the header.")
    args: argparse.Namespace = parser.parse_args()

    pe_image: PeImage = PeImage(args.exe_path)
    signature_config: dict[str, Any] = load_signatures(args.signatures)
    resolved_values: dict[str, int] = {}

    for symbol_name, symbol_config_any in signature_config["symbols"].items():
        symbol_config: dict[str, Any] = dict(symbol_config_any)
        result: MatchResult = resolve_symbol(pe_image, symbol_name, symbol_config)
        resolved_values[symbol_name] = result.value
        print(f"{symbol_name} = 0x{result.value:X} ({result.section_name}, file+0x{result.file_offset:X})")

    if args.dry_run:
        print("Dry run complete. Header was not modified.")
        return 0

    original_text: str = args.header_path.read_text(encoding="utf-8")
    updated_text: str
    updated_symbols: list[str]
    updated_text, updated_symbols = update_header_text(original_text, resolved_values)

    if updated_text == original_text:
        print("No header changes needed.")
        return 0

    if not args.no_backup:
        backup_path: Path = args.header_path.with_suffix(args.header_path.suffix + ".bak")
        shutil.copy2(args.header_path, backup_path)
        print(f"Backup written: {backup_path}")

    args.header_path.write_text(updated_text, encoding="utf-8", newline="")
    print(f"Updated {len(updated_symbols)} symbols in {args.header_path}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        print(f"error: {exception}", file=sys.stderr)
        raise SystemExit(1)
