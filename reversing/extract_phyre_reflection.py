"""
Recover struct layouts and method names from PhyreEngine's runtime reflection metadata.

Run inside IDA (File > Script file, or exec it from the IDAPython console) with FFX.exe open.

PhyreEngine registers every serializable class at startup with its name, size, and each member's
name and byte offset, because the .phyre asset format serializes against that metadata. Walking
the registration call sites recovers the original field names instead of guessed ones.

Outputs, next to the IDB:
  phyre_types.h               C structs, also parsed into the IDB as local types
  phyre_reflection_meta.json  raw extraction (sizes, descriptor maps, member lists)

The five helper addresses below are specific to the Steam HD Remaster build. After a game patch,
re-find them rather than assuming they held:
  - AddDataMember is the function with ~750 xrefs whose call sites push a "m_*" string.
  - Init is the one with ~1100 xrefs that takes (name, size, align) and is called with ecx set to
    a .data global that later receives a ??_7?$PClassDescriptor* vtable pointer.
"""

import json
import os
import re
from collections import Counter, defaultdict

import ida_bytes
import ida_funcs
import ida_name
import ida_typeinf
import idaapi
import idautils
import idc

INIT_FULL = 0x43B0A0    # PClassDescriptor::Init(this, ns, name, size, align, base, flags)
INIT_SIMPLE = 0x43AFB0  # PClassDescriptor::Init(this, ns, name, size, align)
ADD_MEMBER = 0x5752A0   # PClassDescriptor::AddDataMember(storage, cls, type, name, off, flags, x)
SET_NAME = 0x43A080     # PClassMember::SetName(member, cls, name, flags)

TEXT_LO, TEXT_HI = 0x401000, 0xB0C000

PRIM = {
    "PUInt32": (4, "unsigned int"), "PInt32": (4, "int"),
    "PUInt16": (2, "unsigned __int16"), "PInt16": (2, "__int16"),
    "PUInt8": (1, "unsigned __int8"), "PInt8": (1, "__int8"),
    "bool": (1, "bool"), "float": (4, "float"), "double": (8, "double"),
    "PType": (4, "void *"),
    # sce::Vectormath Aos vectors are all 16 bytes, including Vector3.
    "Vector3": (16, "PVec4f"), "Vector4": (16, "PVec4f"),
    "Point3": (16, "PVec4f"), "Quat": (16, "PVec4f"), "Vector2": (8, "PVec2f"),
    "PMatrix4x3": (48, "PMat4x3"), "PMatrix4": (64, "PMat4"), "Matrix4": (64, "PMat4"),
}

# Stray 1-char strings ("N", "U") live inside the PType getters; a real type name is >= 3 chars.
_BAD_TYPENAME = re.compile(r"^(m_|set[A-Z]|get[A-Z]|operator)")
_DEFAULT_NAME = re.compile(r"^(sub_|nullsub_|j_sub_|loc_|unknown_libname_|def_)")


def cstr(ea):
    if not ea or not idaapi.is_loaded(ea):
        return None
    s = ida_bytes.get_strlit_contents(ea, -1, 0)
    if not s:
        return None
    try:
        s = s.decode()
    except UnicodeDecodeError:
        return None
    return s if 0 < len(s) < 80 and all(32 <= ord(c) < 127 for c in s) else None


def call_context(call_ea, window=0x90):
    """Pushed immediates preceding a call, in address order, plus the last 'mov ecx, imm'.

    Parse at the disassembly level: the compiler shares pushed argument slots with an intervening
    call that produces one of the arguments, which makes Hex-Rays mis-assign them.
    """
    f = ida_funcs.get_func(call_ea)
    lo = max(f.start_ea, call_ea - window) if f else call_ea - window
    pushes, ecx, ea = [], None, lo
    while ea < call_ea:
        mnem = idc.print_insn_mnem(ea)
        if mnem == "push":
            t = idc.get_operand_type(ea, 0)
            ok = t in (idc.o_imm, idc.o_mem, idc.o_near, idc.o_far)
            pushes.append(idc.get_operand_value(ea, 0) if ok else None)
        elif mnem == "mov" and idc.print_operand(ea, 0) == "ecx":
            t = idc.get_operand_type(ea, 1)
            ecx = idc.get_operand_value(ea, 1) if t in (idc.o_imm, idc.o_mem) else None
        ea = idaapi.next_head(ea, call_ea + 1)
    return pushes, ecx


def pytype_name(fn):
    """A PType getter's type is the first real string literal in its body."""
    f = ida_funcs.get_func(fn)
    if not f:
        return None
    ea = f.start_ea
    while ea < f.end_ea:
        for ref in idautils.DataRefsFrom(ea):
            s = cstr(ref)
            if (s and len(s) >= 3 and not _BAD_TYPENAME.match(s)
                    and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:<>, ]*", s)):
                return s
        ea = idaapi.next_head(ea, f.end_ea)
    return None


def strip_cxx(name):
    for pre in ("const ", "class ", "struct ", "enum "):
        if name.startswith(pre):
            name = name[len(pre):]
    return name.strip()


def collect():
    # --- class sizes, and descriptor globals seen as 'mov ecx' at Init sites ---
    sizes, ecx_names = {}, {}
    for target, nargs in ((INIT_FULL, 6), (INIT_SIMPLE, 4)):
        for xref in idautils.XrefsTo(target):
            ea = xref.frm
            if idc.print_insn_mnem(ea) != "call":
                continue
            pushes, ecx = call_context(ea)
            pushes = pushes[-nargs:]
            name = idx = None
            for i in range(len(pushes) - 1, -1, -1):
                s = cstr(pushes[i])
                if s:
                    name, idx = s, i
                    break
            if name is None or idx < 2:
                continue
            size, align = pushes[idx - 1], pushes[idx - 2]
            if size is not None and 0 < size < 0x20000:
                sizes.setdefault(name, (size, align))
            if ecx:
                ecx_names.setdefault(ecx, name)

    # --- descriptor global -> class name, from the PClassDescriptor<T> vtable it is assigned ---
    tmpl = re.compile(r"PClassDescriptor\w*<(.+)>::")
    desc2class = {}
    for vea, vname in idautils.Names():
        if not (vname.startswith("??_7") and "PClassDescriptor" in vname):
            continue
        m = tmpl.search(ida_name.demangle_name(vname, 0) or vname)
        if not m:
            continue
        cls = strip_cxx(m.group(1))
        for xref in idautils.XrefsTo(vea):
            ea = xref.frm
            if idc.print_insn_mnem(ea) == "mov" and idc.get_operand_type(ea, 0) == idc.o_mem:
                desc2class.setdefault(idc.get_operand_value(ea, 0), cls)

    # --- members ---
    members = defaultdict(list)
    getter_cache = {}
    for xref in idautils.XrefsTo(ADD_MEMBER):
        ea = xref.frm
        if idc.print_insn_mnem(ea) != "call":
            continue
        pushes, _ = call_context(ea, 0x80)
        pushes = pushes[-8:]
        idx = None
        for i in range(len(pushes) - 1, -1, -1):
            if cstr(pushes[i]):
                idx = i
                break
        if idx is None or idx < 2 or idx + 1 >= len(pushes):
            continue
        name, off, flags = cstr(pushes[idx]), pushes[idx - 1], pushes[idx - 2]
        type_desc, class_desc = pushes[idx + 1], pushes[-1]
        if off is None or class_desc is None:
            continue

        if type_desc is not None:
            tname = desc2class.get(type_desc)
        else:
            # typeDesc came back in eax from a PType getter called between the pushes
            getter, f = None, ida_funcs.get_func(ea)
            scan = max(f.start_ea, ea - 0x80)
            while scan < ea:
                if idc.print_insn_mnem(scan) == "call":
                    t = idc.get_operand_value(scan, 0)
                    if t != ADD_MEMBER:
                        getter = t
                scan = idaapi.next_head(scan, ea)
            if getter is not None and getter not in getter_cache:
                getter_cache[getter] = pytype_name(getter)
            tname = getter_cache.get(getter)

        members[class_desc].append(
            dict(off=off, name=name, flags=flags, type=tname))

    return sizes, ecx_names, desc2class, members, getter_cache


def build_header(sizes, ecx_names, desc2class, members):
    def cls_name(desc):
        return desc2class.get(desc) or ecx_names.get(desc)

    fullnames = {cls_name(d) for d in members}
    leaf_count = Counter(f.split("::")[-1] for f in fullnames if f)

    def struct_name(full):
        leaf = full.split("::")[-1]
        base = leaf if leaf_count[leaf] == 1 else full.replace("::", "_")
        return re.sub(r"[^A-Za-z0-9_]", "_", base)

    classes = {}
    for desc, ms in members.items():
        full = cls_name(desc)
        if not full:
            continue
        total = sizes.get(full.split("::")[-1], (None, None))[0]
        classes[struct_name(full)] = dict(
            full=full, size=total, desc=desc, ms=sorted(ms, key=lambda m: m["off"]))

    # types referenced by a member but carrying no reflected members of their own
    opaque = {}
    for c in classes.values():
        for m in c["ms"]:
            t = m["type"]
            if not t or t in PRIM:
                continue
            leaf = t.split("::")[-1]
            if leaf in classes:
                continue
            s = sizes.get(leaf)
            if s and 0 < s[0] <= 0x2000:
                opaque[leaf] = s[0]

    def tsize(t):
        if t in PRIM:
            return PRIM[t][0]
        if t is None:
            return None
        leaf = t.split("::")[-1]
        if leaf in classes:
            return classes[leaf]["size"]
        if leaf in opaque:
            return opaque[leaf]
        s = sizes.get(leaf)
        return s[0] if s else None

    def tname(t):
        if t in PRIM:
            return PRIM[t][1]
        if t is None:
            return None
        leaf = t.split("::")[-1]
        return leaf if (leaf in classes or leaf in opaque) else None

    def field(t, slot):
        size, name, is_prim = tsize(t), tname(t), t in PRIM
        if name and size == slot:
            return name, slot
        # a type bigger than its slot is stored by pointer
        if slot == 4 and name and not is_prim:
            return name + " *", 4
        if slot == 4 and t is None:
            return "void *", 4
        if name and size and size < slot:
            return name, size
        return None, slot

    built, deps = {}, {}
    for sname, c in classes.items():
        total, seen, ms = c["size"], set(), []
        for m in c["ms"]:
            if m["off"] in seen:      # overlapping registration, keep the first
                continue
            seen.add(m["off"])
            ms.append(m)
        fields, dep, cur = [], set(), 0
        for i, m in enumerate(ms):
            off = m["off"]
            if off < cur:
                continue
            if off > cur:
                fields.append("unsigned __int8 _pad_%x[%d];" % (cur, off - cur))
                cur = off
            if i + 1 < len(ms):
                slot = ms[i + 1]["off"] - off
            elif total and total > off:
                slot = total - off
            else:
                slot = tsize(m["type"]) or 4
            if slot <= 0:
                slot = tsize(m["type"]) or 4
            ctype, used = field(m["type"], slot)
            safe = re.sub(r"[^A-Za-z0-9_]", "_", m["name"])
            if ctype is None:
                fields.append("unsigned __int8 %s[%d];  // %s" % (safe, slot, m["type"] or "?"))
            else:
                fields.append("%s %s;" % (ctype, safe))
                if not ctype.endswith("*") and ctype.strip() in classes:
                    dep.add(ctype.strip())
                if used < slot:
                    fields.append("unsigned __int8 _pad_%x[%d];" % (off + used, slot - used))
            cur = off + slot
        if total and cur < total:
            fields.append("unsigned __int8 _tail_%x[%d];" % (cur, total - cur))
        built[sname], deps[sname] = fields, dep

    order, state = [], {}

    def visit(n):
        if state.get(n):
            return
        state[n] = 1
        for d in sorted(deps.get(n, ())):
            visit(d)
        state[n] = 2
        order.append(n)

    for n in sorted(built):
        visit(n)

    out = [
        "// Phyre/FFX structs recovered from PhyreEngine reflection metadata in FFX.exe.",
        "// Generated by extract_phyre_reflection.py - do not hand-edit.",
        "struct PVec2f { float x, y; };",
        "struct PVec4f { float x, y, z, w; };",
        "struct PMat4x3 { float m[12]; };",
        "struct PMat4 { float m[16]; };",
        "",
    ]
    out += ["struct %s { unsigned __int8 _opaque[%d]; };" % (k, v)
            for k, v in sorted(opaque.items()) if k not in classes]
    out.append("")
    out += ["struct %s;" % n for n in order]
    out.append("")
    for n in order:
        c = classes[n]
        out.append("// %s  size=%s  descriptor=%s" % (c["full"], c["size"], hex(c["desc"])))
        out.append("struct %s {" % n)
        out += ["  " + f for f in built[n]]
        out.append("};")
        out.append("")
    return "\n".join(out), classes, opaque


def name_reflection_functions(getter_cache):
    """Name the registration helpers, the PType getters, and the per-class descriptor functions."""
    def setname(ea, nm, cmt=None):
        cur = idaapi.get_func_name(ea) or ""
        if cur and not _DEFAULT_NAME.match(cur):
            return False
        base, i = nm, 1
        while not ida_name.set_name(ea, nm, ida_name.SN_NOCHECK | ida_name.SN_NOWARN):
            i += 1
            nm = "%s_%d" % (base, i)
            if i > 6:
                return False
        if cmt:
            idc.set_func_cmt(ea, cmt, 0)
        return True

    count = 0
    for ea, nm in ((INIT_FULL, "Phyre__PClassDescriptor__Init"),
                   (INIT_SIMPLE, "Phyre__PClassDescriptor__InitSimple"),
                   (ADD_MEMBER, "Phyre__PClassDescriptor__AddDataMember"),
                   (SET_NAME, "Phyre__PClassMember__SetName")):
        count += setname(ea, nm)

    for fn, tname in getter_cache.items():
        if tname:
            count += setname(fn, "Phyre__PType__" + re.sub(r"[^A-Za-z0-9_]", "_", tname),
                             "Returns the static PType descriptor for '%s'." % tname)

    for target, nargs in ((INIT_FULL, 6), (INIT_SIMPLE, 4)):
        for xref in idautils.XrefsTo(target):
            ea = xref.frm
            if idc.print_insn_mnem(ea) != "call":
                continue
            f = ida_funcs.get_func(ea)
            if not f:
                continue
            pushes, ecx = call_context(ea)
            pushes = pushes[-nargs:]
            cls = None
            for i in range(len(pushes) - 1, -1, -1):
                s = cstr(pushes[i])
                if s:
                    cls = s
                    break
            if not cls:
                continue
            safe = re.sub(r"[^A-Za-z0-9_]", "_", cls)
            if ecx:
                count += setname(f.start_ea, safe + "__GetClassDescriptor",
                                 "Lazy getter for the PhyreEngine class descriptor of '%s'; "
                                 "descriptor global at %s." % (cls, hex(ecx)))
            else:
                count += setname(f.start_ea, safe + "__ClassDescriptor_ctor",
                                 "Constructor for the PhyreEngine class descriptor of '%s'." % cls)
    return count


def name_reflected_methods():
    """PMethodCallerConcrete<Class, Ret, Args...> vtables sit next to the real method pointer.

    The template argument list is the true signature and the preceding PClassMember::SetName call
    supplies the original method name, so these are exact, not guesses.
    """
    tmpl = re.compile(r"(PMethod|PFunction)CallerConcrete<(.+)>::")
    skip = re.compile(r"^(sub_|nullsub_|j_sub_)|__vf\d\d$")

    def split_args(s):
        out, depth, cur = [], 0, ""
        for ch in s:
            if ch == "<":
                depth += 1
            elif ch == ">":
                depth -= 1
            if ch == "," and depth == 0:
                out.append(cur.strip())
                cur = ""
            else:
                cur += ch
        if cur.strip():
            out.append(cur.strip())
        return out

    def nearest_method_name(ea):
        f = ida_funcs.get_func(ea)
        if not f:
            return None
        best, scan = None, f.start_ea
        while scan < ea:
            if (idc.print_insn_mnem(scan) == "call"
                    and idc.get_operand_value(scan, 0) == SET_NAME):
                lo, p, got = max(f.start_ea, scan - 0x40), None, None
                p = lo
                while p < scan:
                    if idc.print_insn_mnem(p) == "push":
                        t = idc.get_operand_type(p, 0)
                        s = cstr(idc.get_operand_value(p, 0)) if t in (idc.o_imm, idc.o_mem) else None
                        if s and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s):
                            got = s
                    p = idaapi.next_head(p, scan)
                if got:
                    best = got
            scan = idaapi.next_head(scan, ea)
        return best

    named = 0
    for vea, vname in idautils.Names():
        if not (vname.startswith("??_7") and "CallerConcrete" in vname):
            continue
        m = tmpl.search(ida_name.demangle_name(vname, 0) or vname)
        if not m:
            continue
        kind, args = m.group(1), split_args(m.group(2))
        for xref in idautils.XrefsTo(vea):
            ea = xref.frm
            if idc.print_insn_mnem(ea) != "mov" or idc.get_operand_type(ea, 0) != idc.o_mem:
                continue
            nxt = idaapi.next_head(ea, ea + 0x30)
            if idc.print_insn_mnem(nxt) != "mov" or idc.get_operand_type(nxt, 1) != idc.o_imm:
                continue
            fva = idc.get_operand_value(nxt, 1)
            f = ida_funcs.get_func(fva)
            if not f or f.start_ea != fva:
                continue
            meth = nearest_method_name(ea)
            if not meth:
                continue
            if kind == "PMethod":
                klass, ret, params = strip_cxx(args[0]), (args[1] if len(args) > 1 else "void"), args[2:]
            else:
                klass, ret, params = None, (args[0] if args else "void"), args[1:]
            sig = "%s %s%s(%s)" % (ret, (klass + "::") if klass else "", meth, ", ".join(params))
            idc.set_func_cmt(fva, "PhyreEngine reflected method.\n"
                                  "Signature from the PCallerConcrete template: %s" % sig, 0)
            cur = idaapi.get_func_name(fva) or ""
            if cur and not skip.search(cur):
                continue
            prefix = (klass.split("::")[-1] + "__") if klass else "Phyre__"
            base = re.sub(r"[^A-Za-z0-9_]", "_", prefix + meth)
            nm, i = base, 1
            while not ida_name.set_name(fva, nm, ida_name.SN_NOCHECK | ida_name.SN_NOWARN):
                i += 1
                nm = "%s_%d" % (base, i)
                if i > 5:
                    break
            else:
                named += 1
    return named


def name_vtable_slots(skip_classes=()):
    """Mechanical class attribution for vtable slots owned by exactly one class.

    The class is reliable; the slot index is not a recovered name. Slots shared by several vtables
    are skipped, because those are inherited base implementations and would be misattributed.
    """
    def walk(ea, maxn=400):
        out, a = [], ea
        for i in range(maxn):
            if i > 0 and idaapi.has_any_name(ida_bytes.get_flags(a)):
                break
            p = ida_bytes.get_dword(a)
            if not (TEXT_LO <= p < TEXT_HI):
                break
            f = ida_funcs.get_func(p)
            if not f or f.start_ea != p:
                break
            out.append(p)
            a += 4
        return out

    owner, tables = defaultdict(set), {}
    for ea, nm in idautils.Names():
        if not (nm.startswith("??_7") and nm.endswith("6B@")):
            continue
        cls = strip_cxx((ida_name.demangle_name(nm, 0) or nm).split("::`")[0])
        if "PClassDescriptor" in cls:
            continue
        fns = walk(ea)
        if not fns:
            continue
        tables[(cls, ea)] = fns
        for p in fns:
            owner[p].add(cls)

    leaf_count = Counter(c.split("::")[-1] for (c, _) in tables)

    def sname(c):
        leaf = c.split("::")[-1]
        return re.sub(r"[^A-Za-z0-9_]", "_",
                      leaf if leaf_count[leaf] == 1 else c.replace("::", "_"))

    default = re.compile(r"^(sub_|nullsub_|j_sub_)")
    named = 0
    for (cls, vea), fns in tables.items():
        if cls.split("::")[-1] in skip_classes:
            continue
        for i, p in enumerate(fns):
            if len(owner[p]) != 1:
                continue
            if not default.match(idaapi.get_func_name(p) or ""):
                continue
            nm = "%s__vf%02d" % (sname(cls), i)
            if ida_name.set_name(p, nm, ida_name.SN_NOCHECK | ida_name.SN_NOWARN):
                idc.set_func_cmt(p, "vtable slot %d of %s (vtable at %s). Slot attribution is "
                                    "mechanical - rename once the behaviour is known."
                                 % (i, cls, hex(vea)), 0)
                named += 1
    return named


def main():
    out_dir = os.path.dirname(idc.get_idb_path()) or "."
    sizes, ecx_names, desc2class, members, getter_cache = collect()
    print("class sizes: %d | descriptor globals: %d (vtable) / %d (ecx) | classes with members: %d"
          % (len(sizes), len(desc2class), len(ecx_names), len(members)))

    header, classes, opaque = build_header(sizes, ecx_names, desc2class, members)
    hpath = os.path.join(out_dir, "phyre_types.h")
    with open(hpath, "w") as fh:
        fh.write(header)
    errors = ida_typeinf.parse_decls(None, header, None, ida_typeinf.HTI_DCL)
    print("wrote %s (%d structs + %d opaque), parse_decls errors: %d"
          % (hpath, len(classes), len(opaque), errors))

    jpath = os.path.join(out_dir, "phyre_reflection_meta.json")
    with open(jpath, "w") as fh:
        json.dump(dict(
            sizes={k: list(v) for k, v in sizes.items()},
            desc2class={hex(k): v for k, v in desc2class.items()},
            ecx_names={hex(k): v for k, v in ecx_names.items()},
            members={hex(k): sorted(v, key=lambda m: m["off"]) for k, v in members.items()},
        ), fh, indent=1)
    print("wrote %s" % jpath)

    print("named %d reflection/descriptor functions" % name_reflection_functions(getter_cache))
    print("named %d reflected methods (exact names + signatures)" % name_reflected_methods())
    print("named %d vtable slots (mechanical class attribution)" % name_vtable_slots())
    print("done - save the database to keep this")


if __name__ == "__main__":
    main()
