from pathlib import Path
import subprocess


Import("env")

root = Path(env.subst("$PROJECT_DIR"))
output = root / "include" / "generated_emoji_sprites.h"
subprocess.run(["swift", str(root / "tools" / "generate_emoji_sprites.swift"), str(output)], check=True)
