from pathlib import Path
import sys
import uuid
import xml.etree.ElementTree as ET

ROOT_FILTERS = {"public": "Public", "private": "Private", "substrate": "Substrate"}
SOURCE_EXTS = {".c", ".cc", ".cpp", ".cxx"}
HEADER_EXTS = {".h", ".hh", ".hpp", ".hxx", ".inl"}
MANAGED_KINDS = {"ClCompile", "ClInclude", "None"}

def xml_ns(root):
    return root.tag.split("}", 1)[0].strip("{")

def tag(ns, name):
    return f"{{{ns}}}{name}"

def include_path(path, project_dir):
    return str(path.relative_to(project_dir)).replace("/", "\\")

def item_kind(path):
    ext = path.suffix.lower()
    if ext in SOURCE_EXTS:
        return "ClCompile"
    if ext in HEADER_EXTS:
        return "ClInclude"
    return "None"

def managed_path(value):
    if not value:
        return False
    normalized = value.replace("/", "\\").lower()
    return any(normalized == root or normalized.startswith(root + "\\") for root in ROOT_FILTERS)

def load_xml(path):
    tree = ET.parse(path)
    root = tree.getroot()
    return tree, root, xml_ns(root)

def save_xml(tree, path):
    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)

def collect_files(project_dir):
    files = []
    for folder in ROOT_FILTERS:
        base = project_dir / folder
        if base.exists():
            files += [path for path in base.rglob("*") if path.is_file()]
    return sorted(files, key=lambda path: include_path(path, project_dir).lower())

def remove_empty_groups(root, ns):
    for group in list(root.findall(tag(ns, "ItemGroup"))):
        if len(list(group)) == 0 and group.get("Label") is None:
            root.remove(group)

def remove_managed_items(root, ns):
    for group in root.findall(tag(ns, "ItemGroup")):
        for node in list(group):
            kind = node.tag.split("}", 1)[-1]
            include = node.get("Include")
            if kind in MANAGED_KINDS and managed_path(include):
                group.remove(node)
    remove_empty_groups(root, ns)

def remove_managed_filters(root, ns):
    for group in root.findall(tag(ns, "ItemGroup")):
        for node in list(group):
            kind = node.tag.split("}", 1)[-1]
            include = node.get("Include")
            if kind == "Filter" and managed_path(include):
                group.remove(node)
            elif kind in MANAGED_KINDS and managed_path(include):
                group.remove(node)
    remove_empty_groups(root, ns)

def get_group(root, ns, kind):
    for group in root.findall(tag(ns, "ItemGroup")):
        if group.find(tag(ns, kind)) is not None:
            return group
    return ET.SubElement(root, tag(ns, "ItemGroup"))

def filter_for(path, project_dir):
    relative = path.relative_to(project_dir)
    current = ROOT_FILTERS[relative.parts[0].lower()]
    for part in relative.parts[1:-1]:
        current += "\\" + part
    return current

def filter_names(files, project_dir):
    names = set(ROOT_FILTERS.values())
    for path in files:
        current = ROOT_FILTERS[path.relative_to(project_dir).parts[0].lower()]
        for part in path.relative_to(project_dir).parts[1:-1]:
            current += "\\" + part
            names.add(current)
    return sorted(names, key=lambda name: (name.count("\\"), name.lower()))

def add_project_items(root, ns, files, project_dir):
    groups = {kind: get_group(root, ns, kind) for kind in MANAGED_KINDS}
    for path in files:
        kind = item_kind(path)
        ET.SubElement(groups[kind], tag(ns, kind), {"Include": include_path(path, project_dir)})

def add_filter_definitions(root, ns, names):
    group = get_group(root, ns, "Filter")
    for name in names:
        node = ET.SubElement(group, tag(ns, "Filter"), {"Include": name})
        uid = ET.SubElement(node, tag(ns, "UniqueIdentifier"))
        uid.text = "{" + str(uuid.uuid4()).upper() + "}"

def add_filter_items(root, ns, files, project_dir):
    groups = {kind: get_group(root, ns, kind) for kind in MANAGED_KINDS}
    for path in files:
        kind = item_kind(path)
        node = ET.SubElement(groups[kind], tag(ns, kind), {"Include": include_path(path, project_dir)})
        filter_node = ET.SubElement(node, tag(ns, "Filter"))
        filter_node.text = filter_for(path, project_dir)

def main():
    project_path = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd().glob("*.vcxproj").__next__().resolve()
    filters_path = project_path.with_name(project_path.name + ".filters")
    project_dir = project_path.parent
    ET.register_namespace("", "http://schemas.microsoft.com/developer/msbuild/2003")
    files = collect_files(project_dir)
    project_tree, project_root, project_ns = load_xml(project_path)
    remove_managed_items(project_root, project_ns)
    add_project_items(project_root, project_ns, files, project_dir)
    save_xml(project_tree, project_path)
    filters_tree, filters_root, filters_ns = load_xml(filters_path)
    remove_managed_filters(filters_root, filters_ns)
    add_filter_definitions(filters_root, filters_ns, filter_names(files, project_dir))
    add_filter_items(filters_root, filters_ns, files, project_dir)
    save_xml(filters_tree, filters_path)
    print(f"Synced {len(files)} files")

if __name__ == "__main__":
    main()
