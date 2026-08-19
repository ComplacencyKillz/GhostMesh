#!/usr/bin/env python3
"""Restore a GhostMesh backpack's config from an encrypted .gmb backup.

Companion to the FAP's Backup screen. After you reflash a wiped backpack with
firmware.factory.bin, this decrypts the backup captured on the Flipper SD and pushes the
Config / ModuleConfig / Channel(s) — including the private-channel PSK — back over serial.

  pip install meshtastic cryptography
  python tools/restore_backpack.py backup_f69c.gmb --port /dev/ttyUSB0

The .gmb file format (little-endian), written by flipper-app/helpers/gm_backup.c:
  "GMBK"(4) version(1) kdf_iters(4) salt(16) iv(12) tag(16) ct_len(2) ciphertext(ct_len)
Key derivation and AES-256-GCM below mirror the FAP byte-for-byte.
"""
import argparse
import getpass
import hashlib
import struct
import sys


def derive_key(salt: bytes, passphrase: str, iters: int) -> bytes:
    """key = SHA256^iters(salt || passphrase) — identical to gm_backup.c derive_key()."""
    h = hashlib.sha256(salt + passphrase.encode()).digest()
    for _ in range(iters - 1):
        h = hashlib.sha256(h).digest()
    return h


def decrypt_backup(path: str, passphrase: str) -> bytes:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    with open(path, "rb") as f:
        blob = f.read()
    if blob[:4] != b"GMBK":
        sys.exit("Not a GhostMesh backup (bad magic).")
    version = blob[4]
    if version != 1:
        sys.exit(f"Unsupported backup version {version}.")
    iters = struct.unpack_from("<I", blob, 5)[0]
    salt = blob[9:25]
    iv = blob[25:37]
    tag = blob[37:53]
    ct_len = struct.unpack_from("<H", blob, 53)[0]
    ciphertext = blob[55:55 + ct_len]

    key = derive_key(salt, passphrase, iters)
    try:
        # AESGCM expects the tag appended to the ciphertext.
        return AESGCM(key).decrypt(iv, ciphertext + tag, None)
    except Exception:
        sys.exit("Decryption failed — wrong passphrase or corrupt file.")


def parse_records(plain: bytes):
    """Yield (type, protobuf_bytes) for each [type][len_lo][len_hi][bytes] record."""
    pos = 0
    while pos + 3 <= len(plain):
        rtype = plain[pos]
        rlen = plain[pos + 1] | (plain[pos + 2] << 8)
        pos += 3
        if pos + rlen > len(plain):
            break
        yield rtype, plain[pos:pos + rlen]
        pos += rlen


def restore(records, port: str, reboot: bool):
    import meshtastic.serial_interface
    from meshtastic.protobuf import admin_pb2, config_pb2, module_config_pb2, channel_pb2

    # FromRadio field numbers → parser + AdminMessage setter.
    FR_CONFIG, FR_MODULE_CONFIG, FR_CHANNEL = 5, 9, 10

    iface = meshtastic.serial_interface.SerialInterface(devPath=port)
    node = iface.localNode
    n = 0
    for rtype, data in records:
        admin = admin_pb2.AdminMessage()
        if rtype == FR_CONFIG:
            admin.set_config.CopyFrom(config_pb2.Config.FromString(data))
        elif rtype == FR_MODULE_CONFIG:
            admin.set_module_config.CopyFrom(module_config_pb2.ModuleConfig.FromString(data))
        elif rtype == FR_CHANNEL:
            admin.set_channel.CopyFrom(channel_pb2.Channel.FromString(data))
        else:
            continue
        node._sendAdmin(admin)  # NOTE: private API — adjust if your meshtastic lib differs
        n += 1
    print(f"Applied {n} config records.")
    if reboot:
        admin = admin_pb2.AdminMessage()
        admin.reboot_seconds = 5
        node._sendAdmin(admin)
        print("Node rebooting in 5 s.")
    iface.close()


def main():
    ap = argparse.ArgumentParser(description="Restore a GhostMesh backpack from a .gmb backup.")
    ap.add_argument("backup", help="path to backup_XXXX.gmb")
    ap.add_argument("--port", required=True, help="serial port of the (reflashed) backpack")
    ap.add_argument("--no-reboot", action="store_true", help="don't reboot after applying")
    args = ap.parse_args()

    passphrase = getpass.getpass("Backup passphrase: ")
    plain = decrypt_backup(args.backup, passphrase)
    records = list(parse_records(plain))
    if not records:
        sys.exit("No config records in the decrypted backup.")
    print(f"Decrypted OK — {len(records)} records.")
    restore(records, args.port, not args.no_reboot)


if __name__ == "__main__":
    main()
