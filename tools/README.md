# Resource Tools

These scripts convert an input resource set into the runtime `assets/` package
used by the ShikigamiSTG examples and tests. They are development utilities;
the engine does not invoke them automatically during a build.

## Legal Notice

These tools are provided solely for learning and interoperability research
with data that you are legally authorized to access. Do not use them to
infringe copyright, bypass access controls, redistribute copyrighted game
data, or create unauthorized derivative works. You are responsible for
obtaining all necessary permissions and complying with the laws that apply to
your use.

## Requirements

- Python 3.10 or newer
- Pillow (`python -m pip install Pillow`)
- `thdat` from [thtk](https://github.com/thpatch/thtk) when processing DAT
  archives

## Full Conversion

`convert_resources.py` is the normal entry point. It extracts the supported
input archives into a temporary directory, converts images and metadata, and
writes a complete runtime package. The temporary extraction directory is
removed after conversion.

```powershell
python tools/convert_resources.py `
  --game "D:\path\to\authorized\game" `
  --output "build/windows/x64/release/assets"
```

Use `--thdat` when `thdat` is not on `PATH`, or set the `THDAT` environment
variable:

```powershell
python tools/convert_resources.py `
  --game "D:\path\to\authorized\game" `
  --thdat "D:\tools\thtk\thdat.exe" `
  --output "build/windows/x64/release/assets"
```

The optional `--stage N` argument limits conversion to one stage. Without it,
all available stages are converted.

## Sprite Conversion

`convert_sprites.py` is a lower-level utility for processing an already
unpacked directory of ANM files:

```powershell
python tools/convert_sprites.py `
  --input "D:\path\to\unpacked\anm" `
  --output "build/windows/x64/release/assets"
```

`convert_anm.py` and `deploy_resources.py` are compatibility entry points for
the unified converter. Keep generated output outside the source tree and
rebuild it whenever the input resource set changes.
