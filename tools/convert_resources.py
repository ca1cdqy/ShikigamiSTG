#!/usr/bin/env python3
"""Extract a TH06 installation and build ShikigamiSTG runtime assets.

The original game data is read from the user-provided installation directory.
DAT archives are extracted to a temporary directory with thdat. ECL is copied
without conversion; all other TH06 metadata is converted to compact JSON while
textures and audio remain standard PNG/JPEG/WAV files.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

from convert_sprites import get_category


SOUND_FILES = [
    "plst00.wav", "enep00.wav", "pldead00.wav", "power0.wav",
    "power1.wav", "tan00.wav", "tan01.wav", "tan02.wav", "ok00.wav",
    "cancel00.wav", "select00.wav", "gun00.wav", "cat00.wav",
    "lazer00.wav", "lazer01.wav", "enep01.wav", "nep00.wav",
    "damage00.wav", "item00.wav", "kira00.wav", "kira01.wav",
    "kira02.wav", "extend.wav", "timeout.wav", "graze.wav", "powerup.wav",
]
SOUND_ASSET_INDEX = [
    0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 5,
    13, 14, 15, 16, 17, 18, 5, 6, 7, 19, 20, 21, 22, 23, 24, 25,
]
SOUND_ATTENUATION = [
    -1500, -2000, -1200, -1400, -1000, -500, -500, -1700,
    -1700, -1700, -1000, -1000, -1900, -1200, -900, -1500,
    -900, -900, -600, -400, -1100, -900, -1800, -1800,
    -1800, -300, -600, -800, -100, -500, -1000, -1000,
]

TH06_ARCHIVES = {
    "CM": "cm",
    "ED": "ed",
    "IN": "in",
    "MD": "md",
    "ST": "st",
    "TL": "tl",
}


def find_child_case_insensitive(root: Path, name: str) -> Path | None:
    expected = name.casefold()
    return next((path for path in root.iterdir()
                 if path.name.casefold() == expected), None)


def find_thdat(explicit: Path | None) -> Path:
    candidates: list[str | Path] = []
    if explicit is not None:
        candidates.append(explicit)
    environment = os.environ.get("THDAT")
    if environment:
        candidates.append(environment)
    discovered = shutil.which("thdat") or shutil.which("thdat.exe")
    if discovered:
        candidates.append(discovered)
    for candidate in candidates:
        path = Path(candidate).expanduser().resolve()
        if path.is_file():
            return path
    raise FileNotFoundError(
        "thdat was not found; pass --thdat, set THDAT, or add thdat to PATH")


def extract_game(game_root: Path, output_root: Path, thdat: Path) -> None:
    game_root = game_root.expanduser().resolve()
    if not game_root.is_dir():
        raise NotADirectoryError(f"TH06 installation not found: {game_root}")
    output_root.mkdir(parents=True, exist_ok=True)

    for archive_id, directory in TH06_ARCHIVES.items():
        archive_name = f"th06_{archive_id}.dat"
        archive = find_child_case_insensitive(game_root, archive_name)
        if archive is None or not archive.is_file():
            raise FileNotFoundError(
                f"original TH06 archive not found: {archive_name}")
        destination = output_root / directory
        destination.mkdir(parents=True, exist_ok=True)
        # Some Windows builds of thdat do not accept Unicode archive paths.
        # Work from the ASCII-named temporary directory to avoid that limit.
        local_archive = destination / archive_name.lower()
        shutil.copy2(archive, local_archive)
        subprocess.run([str(thdat), "-x6", local_archive.name],
                       cwd=destination, check=True,
                       stdout=subprocess.DEVNULL)
        local_archive.unlink()

    bgm_source = find_child_case_insensitive(game_root, "BGM")
    if bgm_source is None or not bgm_source.is_dir():
        raise FileNotFoundError("TH06 BGM directory not found")
    music_output = output_root / "md"
    for track in sorted(bgm_source.iterdir()):
        if track.is_file() and track.suffix.casefold() == ".wav":
            shutil.copy2(track, music_output / track.name)


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, separators=(",", ":"),
                  sort_keys=True, allow_nan=False)
        stream.write("\n")


def find_source(root: Path, relative_name: str, anm_path: Path) -> Path | None:
    normalized = relative_name.replace("\\", "/").lstrip("@/")
    candidates = [root / normalized, anm_path.parent / Path(normalized).name]
    return next((path for path in candidates if path.is_file()), None)


def crop_available(image: Image.Image,
                   box: tuple[int, int, int, int]) -> Image.Image:
    """Crop the portion present in an unpacked TH06 texture."""
    left, top, right, bottom = box
    available = (max(0, left), max(0, top), min(image.width, right),
                 min(image.height, bottom))
    if available[2] > available[0] and available[3] > available[1]:
        return image.crop(available)

    # A few atlases deliberately place complete sprites after the declared
    # 256-pixel edge and rely on wrapping. Partial overrun, as used by eff04,
    # instead describes the available unpacked background area.
    width = right - left
    height = bottom - top
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid sprite rectangle: {box}")
    result = Image.new("RGBA", (width, height))
    start_x = -(left % image.width)
    start_y = -(top % image.height)
    for y in range(start_y, height, image.height):
        for x in range(start_x, width, image.width):
            result.paste(image, (x, y))
    return result


def parse_anm(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 64:
        raise ValueError(f"short ANM header: {path}")
    sprite_count, script_count = struct.unpack_from("<II", data)
    if 64 + sprite_count * 4 + script_count * 8 > len(data):
        raise ValueError(f"invalid ANM tables: {path}")

    sprites = []
    raw_to_local = {}
    for local_id in range(sprite_count):
        offset, = struct.unpack_from("<I", data, 64 + local_id * 4)
        if offset + 20 > len(data):
            raise ValueError(f"invalid ANM sprite offset: {path}")
        raw_id, x, y, width, height = struct.unpack_from("<Iffff", data, offset)
        raw_to_local[str(raw_id)] = local_id
        sprites.append([raw_id, x, y, width, height])

    table = 64 + sprite_count * 4
    script_entries = [struct.unpack_from("<iI", data, table + index * 8)
                      for index in range(script_count)]
    scripts = {}
    for index, (script_id, begin) in enumerate(script_entries):
        end = script_entries[index + 1][1] if index + 1 < script_count else len(data)
        cursor = begin
        instructions = []
        while cursor + 4 <= end:
            time, opcode, length = struct.unpack_from("<hBB", data, cursor)
            if cursor + 4 + length > end:
                raise ValueError(f"invalid ANM instruction: {path} script {script_id}")
            # Preserve the exact TH06 arguments. Jump offsets stay valid because
            # the runtime reconstructs the same instruction byte layout.
            instructions.append([time, opcode, data[cursor + 4:cursor + 4 + length].hex()])
            cursor += 4 + length
            if opcode == 0 and time == 0:
                break
        scripts[str(script_id)] = instructions

    name_offset, = struct.unpack_from("<I", data, 28)
    secondary_offset, = struct.unpack_from("<I", data, 36)
    def read_c_string(offset: int) -> str:
        if offset <= 0 or offset >= len(data):
            return ""
        return data[offset:data.find(b"\0", offset)].decode("ascii", errors="replace")

    return {
        "version": 1,
        "atlas": path.stem,
        "textures": [name for name in
                     (read_c_string(name_offset), read_c_string(secondary_offset)) if name],
        "spriteMap": raw_to_local,
        "sprites": sprites,
        "scripts": scripts,
    }


def convert_anm(path: Path, source_root: Path, output_root: Path) -> dict:
    animation = parse_anm(path)
    atlas = animation["atlas"]
    category = get_category(atlas)
    textures = []
    if animation["textures"]:
        source = find_source(source_root, animation["textures"][0], path)
        if source is not None:
            image = Image.open(source).convert("RGBA")
            alpha = None
            if len(animation["textures"]) > 1:
                alpha = find_source(source_root, animation["textures"][1], path)
            if alpha is None:
                guessed_alpha = source.with_name(source.stem + "_a.png")
                alpha = guessed_alpha if guessed_alpha.is_file() else None
            if alpha is not None:
                # AnmManager::LoadTextureAlphaChannel copies the blue byte of
                # the auxiliary texture into the destination alpha channel.
                alpha_blue = Image.open(alpha).convert("RGB").getchannel("B")
                image.putalpha(alpha_blue)
            textures.append(image)
    atlas_path = None
    image_assets = []
    if textures:
        sprite_dir = output_root / "texture" / category / atlas
        sprite_dir.mkdir(parents=True, exist_ok=True)
        atlas_sprites = []
        for local_id, sprite in enumerate(animation["sprites"]):
            _, x, y, width, height = sprite
            # TH06 sprite IDs refer to the current ANM texture entry. The
            # extracted archives used by this project contain one color
            # texture per ANM.
            image = textures[0]
            box = (round(x), round(y), round(x + width), round(y + height))
            filename = f"sprite_{local_id:03d}.png"
            image_id = f"th06.image.{atlas}.{local_id}"
            cropped = crop_available(image, box)
            if atlas == "etama3" and 146 <= local_id <= 153:
                # The original laser strip is linearly sampled inside the
                # shared atlas, where dark neighboring texels soften all four
                # edges. Preserve that sampling border after splitting the
                # atlas into standalone textures.
                padded = Image.new("RGBA",
                                   (cropped.width + 2, cropped.height + 2),
                                   (0, 0, 0, 255))
                padded.paste(cropped, (1, 1))
                cropped = padded
            cropped.save(sprite_dir / filename)
            atlas_sprites.append({"asset": image_id, "file": filename,
                                  "height": cropped.height,
                                  "id": local_id, "origin_x": width / 2.0,
                                  "origin_y": height / 2.0,
                                  "width": cropped.width})
            image_assets.append({
                "id": image_id,
                "format": "shiki.image.rgba8.v1",
                "source": f"texture/{category}/{atlas}/{filename}",
            })

        atlas_path = f"texture/{atlas}.json"
        write_json(output_root / atlas_path,
                   {"source": atlas, "sprites": atlas_sprites,
                    "textures": animation["textures"], "version": 1})
    del animation["sprites"]
    del animation["textures"]
    write_json(output_root / "animation" / f"{atlas}.json", animation)
    return {"atlas": atlas_path, "animation": f"animation/{atlas}.json",
            "images": image_assets}


def convert_std(path: Path, output_root: Path) -> str:
    data = path.read_bytes()
    if len(data) < 0x490:
        raise ValueError(f"short STD file: {path}")

    def read_text(offset: int) -> str:
        raw = data[offset:offset + 128].split(b"\0", 1)[0]
        return raw.decode("cp932").strip()

    object_count, = struct.unpack_from("<H", data)
    instances_offset, script_offset = struct.unpack_from("<II", data, 4)
    stage_name = read_text(16)
    song_names = [read_text(16 + 128 * index) for index in range(1, 5)]
    objects = []
    for index in range(object_count):
        offset, = struct.unpack_from("<I", data, 0x490 + index * 4)
        _, z_level = struct.unpack_from("<Bb", data, offset + 1)
        position = list(struct.unpack_from("<fff", data, offset + 4))
        quads = []
        cursor = offset + 28
        while True:
            quad_type, size = struct.unpack_from("<hh", data, cursor)
            if quad_type < 0:
                break
            if size < 28 or cursor + size > len(data):
                raise ValueError(f"invalid STD quad: {path}")
            script, = struct.unpack_from("<h", data, cursor + 4)
            values = struct.unpack_from("<fffff", data, cursor + 8)
            quads.append([script, *values])
            cursor += size
        objects.append({"z": z_level, "position": position, "quads": quads})

    instances = []
    cursor = instances_offset
    while cursor + 16 <= len(data):
        object_id, = struct.unpack_from("<h", data, cursor)
        if object_id < 0:
            break
        instances.append([object_id, *struct.unpack_from("<fff", data, cursor + 4)])
        cursor += 16

    timeline = []
    cursor = script_offset
    while cursor + 20 <= len(data):
        frame, opcode, size = struct.unpack_from("<Ihh", data, cursor)
        if size < 0:
            break
        arg0, = struct.unpack_from("<i", data, cursor + 8)
        vector = tuple(value if math.isfinite(value) else 0.0
                       for value in struct.unpack_from("<fff", data, cursor + 8))
        timeline.append([frame, opcode, arg0, *vector])
        cursor += 20
    relative = f"stage/{path.stem}.json"
    write_json(output_root / relative,
               {"version": 1, "atlas": path.stem.replace("stage", "stg") + "bg",
                "name": stage_name, "songs": song_names,
                "objects": objects, "instances": instances, "timeline": timeline})
    return relative


def convert_dialogue(path: Path, output_root: Path) -> str:
    data = path.read_bytes()
    count, = struct.unpack_from("<i", data)
    offsets = struct.unpack_from(f"<{count}I", data, 4)
    messages = []
    for message_index, begin in enumerate(offsets):
        end = offsets[message_index + 1] if message_index + 1 < count else len(data)
        cursor = begin
        commands = []
        while cursor + 4 <= end:
            time, opcode, length = struct.unpack_from("<HBB", data, cursor)
            args = data[cursor + 4:cursor + 4 + length]
            if len(args) != length:
                raise ValueError(f"invalid dialogue command: {path}")
            values: list[int | str] = []
            if opcode in (1, 2, 5):
                values = list(struct.unpack_from("<hh", args))
            elif opcode == 3:
                portrait, line = struct.unpack_from("<hh", args)
                text = args[4:].rstrip(b"\0").decode("cp932")
                values = [portrait, line, text]
            elif opcode in (4, 7, 9, 13):
                values = [struct.unpack_from("<i", args)[0]]
            elif args:
                values = [args.hex()]
            commands.append([time, opcode, *values])
            cursor += 4 + length
            if opcode == 0:
                break
        messages.append(commands)
    relative = f"dialogue/{path.stem}.json"
    write_json(output_root / relative, {"version": 1, "messages": messages})
    return relative


def convert_audio(source_root: Path, output_root: Path) -> str:
    sounds_dir = output_root / "sounds"
    sounds_dir.mkdir(parents=True, exist_ok=True)
    sounds = []
    for sound_id, (asset_index, attenuation) in enumerate(
            zip(SOUND_ASSET_INDEX, SOUND_ATTENUATION)):
        filename = SOUND_FILES[asset_index]
        source = next(source_root.rglob(filename), None)
        if source is None:
            raise FileNotFoundError(filename)
        destination = sounds_dir / filename
        if not destination.exists():
            shutil.copy2(source, destination)
        sounds.append({"file": f"sounds/{filename}",
                       "gain": round(math.pow(10.0, attenuation / 2000.0), 6),
                       "id": sound_id})

    music_dir = output_root / "bgm"
    music_dir.mkdir(parents=True, exist_ok=True)
    music = []
    for track_id in range(1, 18):
        stem = f"th06_{track_id:02d}"
        wav = next(source_root.rglob(stem + ".wav"), None)
        pos = next(source_root.rglob(stem + ".pos"), None)
        if pos is None:
            raise FileNotFoundError(stem + ".pos")
        filename = stem + ".wav"
        destination = music_dir / filename
        if wav is not None:
            if not destination.exists():
                shutil.copy2(wav, destination)
        if not destination.is_file():
            continue
        start, end = struct.unpack_from("<ii", pos.read_bytes())
        music.append({"file": f"bgm/{filename}", "id": stem,
                      "loopEnd": end, "loopStart": start})
    relative = "audio/audio.json"
    write_json(output_root / relative,
               {"version": 1, "sounds": sounds, "music": music})
    return relative


def convert(source_root: Path, output_root: Path,
            selected_stage: int | None) -> None:
    source_root = source_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    # The output is a generated runtime package. Remove stale proprietary
    # metadata left by older deployment scripts so ECL is the sole binary
    # script format shipped to the game.
    for extension in ("*.anm", "*.std", "*.dat", "*.pos"):
        for stale in output_root.rglob(extension):
            stale.unlink()
    animations = {}
    for path in sorted(source_root.rglob("*.anm")):
        converted = convert_anm(path, source_root, output_root)
        animations[path.stem] = converted

    stage_dir = source_root / "st"
    stage_numbers = ([selected_stage] if selected_stage is not None else
                     sorted(int(path.stem.removeprefix("stage"))
                            for path in stage_dir.glob("stage*.std")))
    stages = {}
    dialogues = {}
    for stage in stage_numbers:
        stage_file = stage_dir / f"stage{stage}.std"
        dialogue_file = stage_dir / f"msg{stage}.dat"
        if not stage_file.is_file() or not dialogue_file.is_file():
            raise FileNotFoundError(f"missing stage {stage} STD or dialogue")
        stages[str(stage)] = convert_std(stage_file, output_root)
        dialogues[str(stage)] = convert_dialogue(dialogue_file, output_root)
    audio = convert_audio(source_root, output_root)

    ecl_dir = output_root / "scripts" / "ecl"
    ecl_dir.mkdir(parents=True, exist_ok=True)
    ecl = {}
    for path in sorted(stage_dir.glob("*.ecl")):
        destination = ecl_dir / path.name
        shutil.copy2(path, destination)
        ecl[path.stem] = f"scripts/ecl/{path.name}"
    for path in sorted((source_root / "tl").glob("*.jpg")):
        destination = output_root / "texture" / "title" / path.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)

    assets = []
    for atlas, converted in sorted(animations.items()):
        images = converted["images"]
        assets.extend(images)
        if converted["atlas"] is not None:
            assets.append({
                "dependencies": [image["id"] for image in images],
                "format": "shiki.sprite_atlas.json.v1",
                "id": f"atlas.{atlas}",
                "source": converted["atlas"],
            })
        assets.append({
            "dependencies": ([f"atlas.{atlas}"]
                             if converted["atlas"] is not None else []),
            "format": "shiki.animation.json.v1",
            "id": f"animation.{atlas}",
            "source": converted["animation"],
        })
    for identifier, relative in sorted(ecl.items()):
        assets.append({"format": "shiki.compat.th06.ecl.v1",
                       "id": f"th06.ecl.{identifier}", "source": relative})
    for identifier, relative in sorted(stages.items()):
        assets.append({"format": "shiki.stage_background.json.v1",
                       "id": f"th06.stage.{identifier}", "source": relative})
    for identifier, relative in sorted(dialogues.items()):
        assets.append({"format": "shiki.dialogue.json.v1",
                       "id": f"th06.dialogue.{identifier}",
                       "source": relative})
    assets.append({"format": "shiki.audio_manifest.json.v1",
                   "id": "th06.audio", "source": audio})

    legacy_animations = {
        name: {"atlas": value["atlas"], "animation": value["animation"]}
        for name, value in animations.items()
    }
    write_json(output_root / "manifest.json",
               {"version": 1, "animations": legacy_animations,
                "assets": assets, "audio": audio, "dialogues": dialogues,
                "ecl": ecl, "stages": stages})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True,
                        help="original TH06 installation directory")
    parser.add_argument("--thdat", type=Path,
                        help="path to thdat (otherwise use THDAT or PATH)")
    parser.add_argument("--output", type=Path, required=True,
                        help="runtime assets directory")
    parser.add_argument("--stage", default="all",
                        help="stage number to convert, or 'all'")
    args = parser.parse_args()
    selected_stage = None if args.stage.lower() == "all" else int(args.stage)
    thdat = find_thdat(args.thdat)
    with tempfile.TemporaryDirectory(prefix="shiki-th06-") as temporary:
        unpacked = Path(temporary) / "unpacked"
        extract_game(args.game, unpacked, thdat)
        convert(unpacked, args.output, selected_stage)


if __name__ == "__main__":
    main()
