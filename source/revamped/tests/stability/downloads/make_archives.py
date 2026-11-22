"""Small mod archives for the download stress run (test only).

    py -3.12 -I make_archives.py <folder>
"""
import os
import random
import struct
import sys
import zipfile


def plugin():
    hedr = b"HEDR" + struct.pack("<H", 12) + struct.pack("<fiI", 1.71, 1, 0x800)
    return b"TES4" + struct.pack("<IIIIHH", len(hedr), 0, 0, 0, 44, 0) + hedr


def archive(path, files):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for name, data in files:
            z.writestr(name, data)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(3)
    blob = lambda n: bytes(rng.getrandbits(8) for _ in range(n))
    # ~1.5 MB each, incompressible: a few seconds at the throttled rate
    for i in range(6):
        archive(os.path.join(out, f"dlmod{i}.zip"),
                [(f"dlmod{i}.esp", plugin()), (f"textures/dlmod{i}/big.dds", blob(1_500_000))])
    # paths inside past MAX_PATH once under the mods folder
    deep = "/".join(["a_rather_long_folder_name_for_testing"] * 8)
    archive(os.path.join(out, "deepmod.zip"),
            [("deepmod.esp", plugin()), (f"meshes/{deep}/file_with_a_long_name_too.nif", b"nif" * 100)])
    # Unicode inside and in the archive's name
    archive(os.path.join(out, "Ñandú Ünïcødé 日本語.zip"),
            [("Ñandú.esp", plugin()), ("textures/Ünïcødé/日本語.dds", b"DDS " + bytes(124))])
    # the same mod name twice, the second with another file (replace / merge)
    archive(os.path.join(out, "dupmod.zip"), [("dupmod.esp", plugin()), ("textures/dup/one.dds", b"1" * 1000)])
    archive(os.path.join(out, "dupmod-v2.zip"), [("dupmod.esp", plugin()), ("textures/dup/two.dds", b"2" * 1000)])
    print("archives in", out)


if __name__ == "__main__":
    main()
