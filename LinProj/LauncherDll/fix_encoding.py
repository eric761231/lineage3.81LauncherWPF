import os

file_path = r'c:\python_training\LinProject3.8\LinProj\LauncherDll\LauncherDll.cpp'

# Read as UTF-8 (which is what the LLM likely wrote)
try:
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
except UnicodeDecodeError:
    # If it fails, maybe it's already in CP950
    with open(file_path, 'r', encoding='cp950') as f:
        content = f.read()

# Write as UTF-8 with BOM
with open(file_path, 'w', encoding='utf-8-sig') as f:
    f.write(content)

print(f"File {file_path} converted to UTF-8 with BOM.")
