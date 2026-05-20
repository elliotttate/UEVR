import ida_auto
import ida_funcs
import ida_hexrays
import ida_lines
import ida_kernwin
import ida_pro
import ida_ua
import idaapi
import idc

OUT = r"E:\Github\Subnautica 2\moddingkit\ida_fault_dump.txt"
FAULT_RVA = 0x2FD1167
FUNC_RVA = 0x2FD10D0


def clean(line):
    return ida_lines.tag_remove(line or "")


def main():
    image_base = idaapi.get_imagebase()
    fault = image_base + FAULT_RVA
    func_start = image_base + FUNC_RVA
    fn = ida_funcs.get_func(fault) or ida_funcs.get_func(func_start)

    lines = []
    lines.append(f"image_base=0x{image_base:x}")
    lines.append(f"fault=0x{fault:x} rva=0x{FAULT_RVA:x}")
    lines.append(f"func_start_hint=0x{func_start:x} rva=0x{FUNC_RVA:x}")

    lines.append(f"function={'<not found>' if fn is None else f'0x{fn.start_ea:x}..0x{fn.end_ea:x} rva=0x{fn.start_ea - image_base:x}'}")
    lines.append("")
    lines.append("linear disasm:")
    ea = func_start
    end = func_start + 0x180
    while ea < end:
        ida_ua.create_insn(ea)
        line = clean(idc.generate_disasm_line(ea, 0))
        size = ida_ua.decode_insn(ida_ua.insn_t(), ea)
        marker = "=>" if ea == fault else "  "
        lines.append(f"{marker} 0x{ea - image_base:08x}: {line}")
        ea += max(size, 1)

    if fn is not None:
        lines.append("")
        lines.append("pseudocode:")
        try:
            if ida_hexrays.init_hexrays_plugin():
                cfunc = ida_hexrays.decompile(fn.start_ea)
                if cfunc is not None:
                    for line in cfunc.get_pseudocode():
                        lines.append(clean(line.line))
                else:
                    lines.append("<decompile returned None>")
            else:
                lines.append("<hexrays unavailable>")
        except Exception as e:
            lines.append(f"<decompile failed: {e}>")

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    ida_pro.qexit(0)


main()
