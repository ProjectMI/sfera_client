import json
import re
import sys
from pathlib import Path

FUNC_RE = re.compile(r"; FUNC\s+(0x[0-9A-Fa-f]+)\s+(\S+)\s+size=(\d+)\s+segment=(\S+)")
END_RE = re.compile(r"; END_FUNC\s+(0x[0-9A-Fa-f]+)\s+(\S+)")

def norm_ea(ea):
    return f"0x{int(ea, 16):08X}"

def human_bytes(n):
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.2f} KB"
    return f"{n / 1024 / 1024:.2f} MB"

def load_cut(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    result = set()
    for item in data:
        if "ea" in item:
            result.add(norm_ea(item["ea"]))
    return result

def parse_asm(path):
    lines = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    funcs = []
    current = None
    for line in lines:
        m = FUNC_RE.match(line)
        if m:
            current = {"ea": norm_ea(m.group(1)), "name": m.group(2), "size": int(m.group(3)), "segment": m.group(4), "lines": 0}
            continue
        if current is not None:
            current["lines"] += 1
            if END_RE.match(line):
                funcs.append(current)
                current = None
    return funcs

def main():
    asm_path = sys.argv[1] if len(sys.argv) > 1 else "asm_input.txt"
    cut_path = sys.argv[2] if len(sys.argv) > 2 else "cut_functions_current.json"

    funcs = parse_asm(asm_path)
    cut = load_cut(cut_path)

    total_count = len(funcs)
    cut_funcs = [f for f in funcs if f["ea"] in cut]
    left_funcs = [f for f in funcs if f["ea"] not in cut]

    total_size = sum(f["size"] for f in funcs)
    cut_size = sum(f["size"] for f in cut_funcs)
    left_size = sum(f["size"] for f in left_funcs)

    total_lines = sum(f["lines"] for f in funcs)
    cut_lines = sum(f["lines"] for f in cut_funcs)
    left_lines = sum(f["lines"] for f in left_funcs)

    missing_in_asm = sorted(cut - {f["ea"] for f in funcs})

    print("ASM cleanup progress")
    print("=" * 64)
    print(f"ASM file: {asm_path}")
    print(f"Cut JSON: {cut_path}")
    print()
    print(f"Functions total:   {total_count}")
    print(f"Functions cut:     {len(cut_funcs)}")
    print(f"Functions left:    {len(left_funcs)}")
    print(f"Progress count:    {len(cut_funcs) / total_count * 100:.2f}%" if total_count else "Progress count:    n/a")
    print()
    print(f"Bytes total:       {total_size} ({human_bytes(total_size)})")
    print(f"Bytes cut:         {cut_size} ({human_bytes(cut_size)})")
    print(f"Bytes left:        {left_size} ({human_bytes(left_size)})")
    print(f"Progress bytes:    {cut_size / total_size * 100:.2f}%" if total_size else "Progress bytes:    n/a")
    print()
    print(f"ASM lines total:   {total_lines}")
    print(f"ASM lines cut:     {cut_lines}")
    print(f"ASM lines left:    {left_lines}")
    print(f"Progress lines:    {cut_lines / total_lines * 100:.2f}%" if total_lines else "Progress lines:    n/a")

    if missing_in_asm:
        print()
        print(f"Warning: {len(missing_in_asm)} cut JSON entries were not found in ASM export")
        for ea in missing_in_asm[:20]:
            print(f"  {ea}")
        if len(missing_in_asm) > 20:
            print(f"  ... and {len(missing_in_asm) - 20} more")

    print()
    print("Largest remaining functions:")
    for f in sorted(left_funcs, key=lambda x: x["size"], reverse=True)[:20]:
        print(f"  {f['ea']}  {f['name']:<32} {f['size']:>8} B  {f['lines']:>6} lines")

def should_pause_on_exit():
    if sys.platform != "win32":
        return False
    try:
        import ctypes
        count = ctypes.c_ulong()
        ctypes.windll.kernel32.GetConsoleProcessList(ctypes.byref(count), 1)
        return count.value <= 1
    except Exception:
        return True

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print()
        print("ERROR:", e)
    finally:
        if should_pause_on_exit():
            print()
            input("Press Enter to exit...")