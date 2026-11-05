"""Print the trusted local checksum and OTA shell command; no third-party modules."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib

p = argparse.ArgumentParser()
p.add_argument('image', type=Path, help='SDK-generated ota.cpio (NOT the USB .img)')
p.add_argument('--url', default='http://192.168.1.100:8000/ota.cpio')
args = p.parse_args()
data = args.image.read_bytes()
if not data.startswith(b'070702'):
    p.error('Expected crc-CPIO OTA package')
manifest = {'file': args.image.name, 'bytes': len(data),
            'crc32': f'{zlib.crc32(data):08X}', 'sha256': hashlib.sha256(data).hexdigest()}
print(json.dumps(manifest, indent=2))
print(f'netapp_ota {args.url} {manifest["crc32"]}')
