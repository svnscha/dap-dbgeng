from __future__ import annotations

# =============================================================================
# protogen.py - DAP protocol generator (C++ backend)
#
# DAP protocol generator: emits the C++20 nlohmann/json protocol types from the
# DAP schema. Reads protgen/schema/debugAdapterProtocol.json and emits the types
# into the output directory (default: src/protocol).
#
# Design:
#   * Each DAP definition becomes a *flattened* struct (all inherited + own
#     fields), so we avoid C++ member-shadowing of the schema's field overrides.
#   * Types are PascalCase (Thread, LaunchRequest); fields are snake_case with
#     the JSON wire name preserved by explicit to_json/from_json.
#   * Optional fields -> std::optional<T>; the raw-JSON fallback -> nlohmann
#     ::json (null == absent). oneOf/anyOf unions -> raw nlohmann::json.
#   * Output is a single header `protocol.h` (enums, fwd decls, structs in
#     topological order over hard edges, then all serializers) plus a generated
#     `dap_service.h` providing one string-dispatched service base class.
# =============================================================================

import argparse
import json
import keyword
import re
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
SCHEMA_PATH = ROOT / "schema" / "debugAdapterProtocol.json"
DEFAULT_OUTPUT = ROOT.parent / "src" / "protocol"
NAMESPACE = "dap_dbgeng::protocol"

CATEGORY_TITLES = {
    "Base Protocol": "Base",
    "Events": "Events",
    "Requests": "Requests",
    "Reverse Requests": "ReverseRequests",
    "Types": "Types",
}

CPP_KEYWORDS = set(keyword.kwlist) | {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch",
    "char", "class", "const", "constexpr", "continue", "default", "delete", "do",
    "double", "else", "enum", "explicit", "export", "extern", "false", "float",
    "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
    "new", "operator", "private", "protected", "public", "register", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "template", "this",
    "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "while", "char8_t", "concept", "requires",
}


# --- type model --------------------------------------------------------------


@dataclass(slots=True)
class CppType:
    decl: str                 # full member decl, e.g. 'std::optional<std::string>'
    kind: str                 # scalar|string|enum|struct|vector|map|json
    optional: bool            # wrapped in std::optional
    hard_deps: set            # generated struct names that must be complete before this struct's definition
    soft_deps: set            # generated names referenced behind a container/enum


@dataclass(slots=True)
class Prop:
    json_name: str
    field_name: str
    cpp: CppType
    required: bool
    description: str | None
    default_literal: str | None  # for discriminators / required enums


@dataclass(slots=True)
class EnumValueModel:
    name: str
    serialized_value: str


@dataclass(slots=True)
class EnumModel:
    name: str
    category: str
    description: str | None
    values: list[EnumValueModel] = field(default_factory=list)


@dataclass(slots=True)
class ClassModel:
    name: str
    category: str
    description: str | None
    base_class: str | None = None
    properties: list[Prop] = field(default_factory=list)
    ctor_assignments: list[str] = field(default_factory=list)  # raw 'Name = value;' discriminators


@dataclass(slots=True)
class DispatchEntry:
    discriminator: str
    target_type: str


class Generator:
    def __init__(self, schema: dict[str, Any]) -> None:
        self.schema = schema
        self.definitions: OrderedDict[str, dict[str, Any]] = OrderedDict(schema["definitions"])
        self.category_by_definition = self._classify_definitions()
        self.classes: OrderedDict[str, ClassModel] = OrderedDict()
        self.enums: OrderedDict[str, EnumModel] = OrderedDict()
        self.definition_type_names: dict[str, str] = {}

    # -- schema classification ------------------------------------------------

    def _classify_definitions(self) -> dict[str, str]:
        current = CATEGORY_TITLES["Base Protocol"]
        result: dict[str, str] = {}
        for name, schema in self.definitions.items():
            title = self._extract_title(schema)
            if title in CATEGORY_TITLES:
                current = CATEGORY_TITLES[title]
            result[name] = current
        return result

    def _extract_title(self, schema: dict[str, Any]) -> str | None:
        title = schema.get("title")
        if isinstance(title, str):
            return title
        for entry in schema.get("allOf", []):
            nested = entry.get("title")
            if isinstance(nested, str):
                return nested
        return None

    def generate(self) -> None:
        for name in self.definitions:
            self._ensure_definition_type(name)

    def _ensure_definition_type(self, name: str) -> str:
        if name in self.definition_type_names:
            return self.definition_type_names[name]
        preferred = self._sanitize_identifier(name)
        if preferred in self.classes or preferred in self.enums:
            self.definition_type_names[name] = preferred
            return preferred
        schema = self.definitions[name]
        category = self.category_by_definition[name]
        if self._schema_enum_values(schema):
            type_name = self._ensure_enum(schema, category, name)
        else:
            type_name = self._ensure_class(schema, category, name)
        self.definition_type_names[name] = type_name
        return type_name

    def _ensure_class(self, schema: dict[str, Any], category: str, preferred: str) -> str:
        existing = self._sanitize_identifier(preferred)
        if existing in self.classes:
            return existing
        resolved = self._unique_type_name(preferred)
        if resolved in self.classes:
            return resolved

        class_schema, base_class = self._normalize_object_schema(schema)
        description = self._clean_doc(class_schema.get("description"))
        required = set(class_schema.get("required", []))
        properties = class_schema.get("properties", {})

        model = ClassModel(name=resolved, category=category, description=description, base_class=base_class)
        self.classes[resolved] = model
        inherited_by_json = self._inherited_properties_by_json_name(base_class)

        for prop_name, prop_schema in properties.items():
            inherited = inherited_by_json.get(prop_name)
            enum_values = self._schema_enum_values(prop_schema)
            # A property that overrides an inherited one with a single fixed enum value is a
            # discriminator (type/command/event); record it as a constructor assignment and skip.
            if inherited is not None and len(enum_values) == 1:
                assignment = self._discriminator_assignment(inherited, enum_values[0])
                if assignment is not None:
                    model.ctor_assignments.append(assignment)
                    continue

            cpp = self._map_type(prop_schema, category, resolved, prop_name, prop_name in required)
            default_literal = self._required_enum_default(cpp) if prop_name in required else None
            model.properties.append(
                Prop(
                    json_name=prop_name,
                    field_name=self._field_name(prop_name),
                    cpp=cpp,
                    required=prop_name in required,
                    description=self._clean_doc(prop_schema.get("description")),
                    default_literal=default_literal,
                )
            )
        return resolved

    def _ensure_enum(self, schema: dict[str, Any], category: str, preferred: str) -> str:
        existing = self._sanitize_identifier(preferred)
        if existing in self.enums:
            return existing
        resolved = self._unique_type_name(preferred)
        if resolved in self.enums:
            return resolved
        values = self._schema_enum_values(schema)
        seen: set = set()
        members: list[EnumValueModel] = []
        for index, value in enumerate(values):
            member = self._enum_member_name(value, index)
            original = member
            suffix = 2
            while member in seen:
                member = f"{original}{suffix}"
                suffix += 1
            seen.add(member)
            members.append(EnumValueModel(name=member, serialized_value=value))
        self.enums[resolved] = EnumModel(
            name=resolved, category=category, description=self._clean_doc(schema.get("description")), values=members
        )
        return resolved

    def _normalize_object_schema(self, schema: dict[str, Any]) -> tuple[dict[str, Any], str | None]:
        if "allOf" not in schema:
            return schema, None
        merged_properties: OrderedDict[str, Any] = OrderedDict()
        merged_required: list[str] = []
        merged_description = schema.get("description")
        base_class: str | None = None
        for entry in schema["allOf"]:
            if "$ref" in entry:
                ref_name = self._ref_name(entry["$ref"])
                if base_class is None:
                    base_class = self._ensure_definition_type(ref_name)
                continue
            merged_properties.update(entry.get("properties", {}))
            merged_required.extend(entry.get("required", []))
            merged_description = merged_description or entry.get("description")
        normalized = {
            "type": "object",
            "description": merged_description,
            "properties": merged_properties,
            "required": list(dict.fromkeys(merged_required)),
        }
        return normalized, base_class

    # -- type mapping (C++) ---------------------------------------------------

    def _map_type(self, schema: dict[str, Any], category: str, owner: str, prop: str, required: bool) -> CppType:
        if "$ref" in schema:
            ref = self._ensure_definition_type(self._ref_name(schema["$ref"]))
            return self._ref_cpp_type(ref, required)

        enum_values = self._schema_enum_values(schema)
        if enum_values:
            enum_name = self._ensure_enum(schema, category, self._compose(owner, prop))
            return self._enum_cpp_type(enum_name, required)

        if "oneOf" in schema or "anyOf" in schema:
            return CppType("nlohmann::json", "json", False, set(), set())

        schema_type = schema.get("type")
        if isinstance(schema_type, list):
            non_null = [t for t in schema_type if t != "null"]
            if len(non_null) == 1:
                narrowed = dict(schema)
                narrowed["type"] = non_null[0]
                return self._map_type(narrowed, category, owner, prop, required)
            return CppType("nlohmann::json", "json", False, set(), set())

        if schema_type == "array":
            item = self._map_array_item(schema.get("items", {}), category, owner, prop)
            base = f"std::vector<{item.decl}>"
            decl = f"std::optional<{base}>" if not required else base
            return CppType(decl, "vector", not required, set(), item.hard_deps | item.soft_deps)

        if schema_type == "object" or "properties" in schema or "allOf" in schema:
            if "additionalProperties" in schema:
                value = self._map_additional(schema["additionalProperties"], category, owner, prop)
                base = f"std::map<std::string, {value.decl}>"
                decl = f"std::optional<{base}>" if not required else base
                return CppType(decl, "map", not required, set(), value.hard_deps | value.soft_deps)
            if schema.get("properties") or schema.get("allOf"):
                nested = self._ensure_class(schema, category, self._compose(owner, prop))
                return self._struct_cpp_type(nested, required)
            return CppType("nlohmann::json", "json", False, set(), set())

        if schema_type == "string":
            decl = "std::optional<std::string>" if not required else "std::string"
            return CppType(decl, "string", not required, set(), set())
        if schema_type == "boolean":
            decl = "std::optional<bool>" if not required else "bool"
            return CppType(decl, "scalar", not required, set(), set())
        if schema_type == "integer":
            base = "int" if schema.get("format") == "int32" else "std::int64_t"
            decl = f"std::optional<{base}>" if not required else base
            return CppType(decl, "scalar", not required, set(), set())
        if schema_type == "number":
            decl = "std::optional<double>" if not required else "double"
            return CppType(decl, "scalar", not required, set(), set())

        return CppType("nlohmann::json", "json", False, set(), set())

    def _ref_cpp_type(self, ref: str, required: bool) -> CppType:
        if ref in self.enums:
            return self._enum_cpp_type(ref, required)
        return self._struct_cpp_type(ref, required)

    def _struct_cpp_type(self, name: str, required: bool) -> CppType:
        decl = f"std::optional<{name}>" if not required else name
        # Both a direct and an optional<struct> member require the struct complete at definition time.
        return CppType(decl, "struct", not required, {name}, set())

    def _enum_cpp_type(self, name: str, required: bool) -> CppType:
        decl = f"std::optional<{name}>" if not required else name
        # Enums are emitted before all structs, so they are a soft dependency for ordering.
        return CppType(decl, "enum", not required, set(), {name})

    def _map_array_item(self, item_schema: dict[str, Any], category: str, owner: str, prop: str) -> CppType:
        if not item_schema:
            return CppType("nlohmann::json", "json", False, set(), set())
        return self._map_type(item_schema, category, owner, f"{prop}Item", True)

    def _map_additional(self, schema: Any, category: str, owner: str, prop: str) -> CppType:
        if schema is True or schema is False:
            return CppType("nlohmann::json", "json", False, set(), set())
        return self._map_type(schema, category, owner, f"{prop}Value", True)

    # -- dispatch entries -----------------------------------------------------

    def request_entries(self) -> list[DispatchEntry]:
        return self._dispatch_entries("Request", "command")

    def event_entries(self) -> list[DispatchEntry]:
        return self._dispatch_entries("Event", "event")

    def _dispatch_entries(self, base: str, discriminator_field: str) -> list[DispatchEntry]:
        entries: list[DispatchEntry] = []
        for name, schema in self.definitions.items():
            target = self.definition_type_names.get(name)
            if not target or target not in self.classes:
                continue
            if not self._inherits_from(target, base):
                continue
            normalized, _ = self._normalize_object_schema(schema)
            field_schema = normalized.get("properties", {}).get(discriminator_field)
            values = self._schema_enum_values(field_schema) if isinstance(field_schema, dict) else []
            if len(values) != 1:
                continue
            entries.append(DispatchEntry(discriminator=values[0], target_type=target))
        return sorted(entries, key=lambda e: e.discriminator)

    def response_entries(self) -> list[DispatchEntry]:
        request_map = {e.target_type.removesuffix("Request"): e.discriminator for e in self.request_entries()}
        entries: list[DispatchEntry] = []
        for name in self.definitions:
            if not name.endswith("Response") or name in {"Response", "ErrorResponse"}:
                continue
            target = self.definition_type_names.get(name)
            if not target or target not in self.classes:
                continue
            if not self._inherits_from(target, "Response"):
                continue
            stem = name.removesuffix("Response")
            discriminator = request_map.get(stem)
            if discriminator is None:
                continue
            entries.append(DispatchEntry(discriminator=discriminator, target_type=target))
        return sorted(entries, key=lambda e: e.discriminator)

    def _inherits_from(self, type_name: str, base_type: str) -> bool:
        current = type_name
        while True:
            if current == base_type:
                return True
            model = self.classes.get(current)
            if model is None or model.base_class is None:
                return False
            current = model.base_class

    # -- flattening (resolve inheritance into standalone structs) -------------

    def _base_chain(self, name: str) -> list[str]:
        chain: list[str] = []
        current: str | None = name
        while current is not None:
            chain.append(current)
            model = self.classes.get(current)
            current = model.base_class if model else None
        chain.reverse()  # root -> derived
        return chain

    def flattened_properties(self, name: str) -> list[Prop]:
        by_json: "OrderedDict[str, Prop]" = OrderedDict()
        assignments: list[str] = []
        for cls_name in self._base_chain(name):
            model = self.classes[cls_name]
            assignments.extend(model.ctor_assignments)
            for prop in model.properties:
                by_json[prop.json_name] = prop  # derived overrides base by wire name
        props = [self._clone_prop(p) for p in by_json.values()]
        # Apply discriminator defaults captured from the schema (Name = value;).
        defaults = self._parse_assignments(assignments)
        by_property_name = {self._sanitize_identifier(p.json_name): p for p in props}
        for prop_name, value in defaults.items():
            target = by_property_name.get(prop_name)
            if target is not None:
                target.default_literal = value
        return props

    def _clone_prop(self, p: Prop) -> Prop:
        return Prop(p.json_name, p.field_name, p.cpp, p.required, p.description, p.default_literal)

    def _parse_assignments(self, assignments: list[str]) -> dict[str, str]:
        result: dict[str, str] = {}
        for raw in assignments:
            text = raw.strip().rstrip(";")
            if " = " not in text:
                continue
            lhs, rhs = text.split(" = ", 1)
            result[lhs.strip()] = rhs.strip()
        return result

    def _discriminator_assignment(self, inherited: Prop, value: str) -> str | None:
        prop_name = self._sanitize_identifier(inherited.json_name)
        if inherited.cpp.kind == "string":
            return f"{prop_name} = {json.dumps(value)};"
        if inherited.cpp.kind == "enum":
            base = inherited.cpp.decl.replace("std::optional<", "").rstrip(">")
            return f"{prop_name} = {base}::{self._enum_member_name(value, 0)};"
        return None

    def _required_enum_default(self, cpp: CppType) -> str | None:
        if cpp.kind != "enum" or cpp.optional:
            return None
        if cpp.decl in self.enums:
            return f"{cpp.decl}::{self.enums[cpp.decl].values[0].name}"
        return None

    # -- topological ordering of struct definitions ---------------------------

    def topological_classes(self) -> list[str]:
        hard: dict[str, set] = {}
        for name in self.classes:
            deps: set = set()
            for prop in self.flattened_properties(name):
                deps |= {d for d in prop.cpp.hard_deps if d in self.classes}
            deps.discard(name)
            hard[name] = deps
        ordered: list[str] = []
        done: set = set()
        visiting: set = set()

        def visit(node: str, stack: list[str]) -> None:
            if node in done:
                return
            if node in visiting:
                raise RuntimeError("Hard dependency cycle: " + " -> ".join(stack + [node]))
            visiting.add(node)
            for dep in sorted(hard[node]):
                visit(dep, stack + [node])
            visiting.discard(node)
            done.add(node)
            ordered.append(node)

        for name in self.classes:
            visit(name, [])
        return ordered

    # -- naming helpers -------------------------------------------------------

    def _sanitize_identifier(self, value: str) -> str:
        cleaned = "".join(c if c.isalnum() else " " for c in value)
        words = [w for w in cleaned.split() if w]
        candidate = "".join(w[:1].upper() + w[1:] for w in words) or "GeneratedType"
        if candidate[0].isdigit():
            candidate = f"Type{candidate}"
        return candidate

    def _field_name(self, json_name: str) -> str:
        cleaned = re.sub(r"[^0-9a-zA-Z]+", "_", json_name)
        snake = re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", cleaned)
        snake = re.sub(r"(?<=[A-Z])([A-Z][a-z])", r"_\1", snake)
        snake = re.sub(r"_+", "_", snake).lower().strip("_")
        if not snake:
            snake = "value"
        if snake[0].isdigit():
            snake = f"field_{snake}"
        if snake in CPP_KEYWORDS:
            snake = f"{snake}_"
        return snake

    def _schema_enum_values(self, schema: dict[str, Any]) -> list[str]:
        values = schema.get("_enum") or schema.get("enum")
        if not isinstance(values, list) or not values:
            return []
        if not all(isinstance(v, str) for v in values):
            return []
        return values

    def _enum_member_name(self, value: str, index: int) -> str:
        cleaned = "".join(c if c.isalnum() else " " for c in value)
        words = [w for w in cleaned.split() if w]
        if not words:
            return f"Value{index + 1}"
        candidate = "".join(w[:1].upper() + w[1:] for w in words)
        if candidate[0].isdigit():
            candidate = f"Value{candidate}"
        return candidate

    def _compose(self, owner: str, prop: str) -> str:
        return f"{owner}{self._sanitize_identifier(prop)}"

    def _inherited_properties_by_json_name(self, base_class: str | None) -> dict[str, Prop]:
        if not base_class:
            return {}
        model = self.classes.get(base_class)
        if model is None:
            return {}
        result = self._inherited_properties_by_json_name(model.base_class)
        result.update({p.json_name: p for p in model.properties})
        return result

    def _unique_type_name(self, preferred: str) -> str:
        candidate = self._sanitize_identifier(preferred)
        suffix = 2
        while candidate in self.classes or candidate in self.enums:
            candidate = f"{self._sanitize_identifier(preferred)}{suffix}"
            suffix += 1
        return candidate

    def _ref_name(self, ref_path: str) -> str:
        return ref_path.rsplit("/", maxsplit=1)[-1]

    def _clean_doc(self, value: Any) -> str | None:
        if not isinstance(value, str):
            return None
        return " ".join(value.split())


# --- emission ----------------------------------------------------------------


def _scalar_default(decl: str) -> str:
    if decl == "bool":
        return "false"
    if decl == "double":
        return "0.0"
    return "0"


def _doc(indent: str, text: str | None) -> str:
    if not text:
        return ""
    return f"{indent}// {text}\n"


def render_protocol_header(gen: Generator) -> str:
    out: list[str] = []
    out.append("// <auto-generated /> - produced by protgen/protogen.py; do not edit by hand.\n")
    out.append("#pragma once\n\n")
    out.append("#include <cstdint>\n")
    out.append("#include <map>\n")
    out.append("#include <optional>\n")
    out.append("#include <string>\n")
    out.append("#include <utility>\n")
    out.append("#include <vector>\n\n")
    out.append("#include <nlohmann/json.hpp>\n\n")
    out.append(f"namespace {NAMESPACE}\n{{\n\n")

    # Enums + their serializers.
    for enum in gen.enums.values():
        out.append(_doc("", enum.description))
        out.append(f"enum class {enum.name}\n{{\n")
        out.append(",\n".join(f"    {v.name}" for v in enum.values))
        out.append("\n};\n\n")
    for enum in gen.enums.values():
        to_pairs = ", ".join(f"{{{enum.name}::{v.name}, {json.dumps(v.serialized_value)}}}" for v in enum.values)
        from_pairs = ", ".join(f"{{{json.dumps(v.serialized_value)}, {enum.name}::{v.name}}}" for v in enum.values)
        out.append(
            f"inline void to_json(nlohmann::json& j, const {enum.name}& v)\n{{\n"
            f"    static const std::pair<{enum.name}, const char*> table[] = {{{to_pairs}}};\n"
            f"    for (const auto& entry : table)\n"
            f"        if (entry.first == v) {{ j = entry.second; return; }}\n"
            f'    throw nlohmann::json::other_error::create(501, "invalid {enum.name} value", &j);\n'
            f"}}\n"
            f"inline void from_json(const nlohmann::json& j, {enum.name}& v)\n{{\n"
            f"    const std::string s = j.get<std::string>();\n"
            f"    static const std::pair<const char*, {enum.name}> table[] = {{{from_pairs}}};\n"
            f"    for (const auto& entry : table)\n"
            f"        if (s == entry.first) {{ v = entry.second; return; }}\n"
            f'    throw nlohmann::json::other_error::create(501, "unknown {enum.name} value: " + s, &j);\n'
            f"}}\n\n"
        )

    # Forward declarations.
    out.append("// Forward declarations (definitions follow in dependency order).\n")
    for name in gen.classes:
        out.append(f"struct {name};\n")
    out.append("\n")

    # Struct definitions in topological order over hard edges.
    for name in gen.topological_classes():
        model = gen.classes[name]
        props = gen.flattened_properties(name)
        out.append(_doc("", model.description))
        out.append(f"struct {name}\n{{\n")
        for p in props:
            out.append(_doc("    ", p.description))
            literal = p.default_literal
            if literal is None and p.required and p.cpp.kind == "scalar":
                literal = _scalar_default(p.cpp.decl)
            initializer = f" = {literal}" if literal else ""
            out.append(f"    {p.cpp.decl} {p.field_name}{initializer};\n")
        out.append("};\n\n")

    # Serializer forward declarations, so cross-references between structs resolve via ADL
    # regardless of definition order (nlohmann looks up to_json/from_json at the call site).
    out.append("// Serializer declarations (definitions follow once every struct is complete).\n")
    for name in gen.classes:
        out.append(f"inline void to_json(nlohmann::json& j, const {name}& v);\n")
        out.append(f"inline void from_json(const nlohmann::json& j, {name}& v);\n")
    out.append("\n")

    # Serializers for every struct (all types are complete here).
    for name in gen.classes:
        props = gen.flattened_properties(name)
        out.append(f"inline void to_json(nlohmann::json& j, const {name}& v)\n{{\n")
        out.append("    (void)j; (void)v;\n")
        for p in props:
            out.append(_to_json_field(p))
        out.append("}\n")
        out.append(f"inline void from_json(const nlohmann::json& j, {name}& v)\n{{\n")
        out.append("    (void)j; (void)v;\n")
        for p in props:
            out.append(_from_json_field(p))
        out.append("}\n\n")

    out.append(f"}} // namespace {NAMESPACE}\n")
    return "".join(out)


def _to_json_field(p: Prop) -> str:
    key = json.dumps(p.json_name)
    f = f"v.{p.field_name}"
    if p.cpp.kind == "json":
        return f"    if (!{f}.is_null()) j[{key}] = {f};\n"
    if p.cpp.optional:
        return f"    if ({f}.has_value()) j[{key}] = *{f};\n"
    return f"    j[{key}] = {f};\n"


def _from_json_field(p: Prop) -> str:
    key = json.dumps(p.json_name)
    f = f"v.{p.field_name}"
    if p.cpp.kind == "json":
        return f"    if (auto it = j.find({key}); it != j.end()) {f} = *it;\n"
    if p.cpp.optional:
        base = p.cpp.decl[len("std::optional<"):-1]
        return (
            f"    if (auto it = j.find({key}); it != j.end() && !it->is_null())\n"
            f"        {f} = it->get<{base}>();\n"
        )
    return f"    if (auto it = j.find({key}); it != j.end() && !it->is_null()) it->get_to({f});\n"


def render_service_header(gen: Generator) -> str:
    requests = gen.request_entries()
    events = gen.event_entries()
    responses = gen.response_entries()

    out: list[str] = []
    out.append("// <auto-generated /> - produced by protgen/protogen.py; do not edit by hand.\n")
    out.append("#pragma once\n\n")
    out.append("#include <stdexcept>\n")
    out.append("#include <string>\n\n")
    out.append('#include "protocol/protocol.h"\n\n')
    out.append("// A single string-dispatched DAP service base.\n\n")
    out.append(f"namespace {NAMESPACE}\n{{\n\n")

    out.append(
        "class unhandled_dap_request : public std::runtime_error\n{\n"
        "  public:\n"
        "    unhandled_dap_request(int request_seq, std::string command)\n"
        '        : std::runtime_error("No handler implemented for DAP request " + command),\n'
        "          request_seq(request_seq), command(std::move(command))\n"
        "    {\n    }\n"
        "    int request_seq;\n"
        "    std::string command;\n"
        "};\n\n"
    )

    out.append("class dap_service\n{\n  public:\n    virtual ~dap_service() = default;\n\n")
    out.append(
        "    void handle(const nlohmann::json& message)\n    {\n"
        '        const std::string type = message.value("type", std::string{});\n'
        '        if (type == "request")\n            return dispatch_request(message);\n'
        '        if (type == "event")\n            return dispatch_event(message);\n'
        '        if (type == "response")\n            return dispatch_response(message);\n'
        '        throw std::runtime_error("Unsupported DAP message type: " + type);\n'
        "    }\n\n"
    )

    out.append("  protected:\n")
    for e in requests:
        out.append(
            f"    virtual void {_handler_name(e.target_type)}(const {e.target_type}& request)\n"
            f'    {{\n        throw unhandled_dap_request(request.seq, "{e.discriminator}");\n    }}\n'
        )
    out.append("\n")
    for e in events:
        out.append(
            f"    virtual void {_handler_name(e.target_type)}(const {e.target_type}& event)\n"
            f"    {{\n        (void)event;\n    }}\n"
        )
    out.append("\n")
    for e in responses:
        out.append(
            f"    virtual void {_handler_name(e.target_type)}(const {e.target_type}& response)\n"
            f"    {{\n        (void)response;\n    }}\n"
        )
    out.append("\n")
    out.append(
        "    virtual void handle_request(const Request& request)\n"
        "    {\n        throw unhandled_dap_request(request.seq, request.command);\n    }\n\n"
    )

    out.append("  private:\n")
    out.append("    void dispatch_request(const nlohmann::json& m)\n    {\n")
    out.append('        const std::string command = m.value("command", std::string{});\n')
    for e in requests:
        out.append(
            f'        if (command == "{e.discriminator}")\n'
            f"            return {_handler_name(e.target_type)}(m.get<{e.target_type}>());\n"
        )
    out.append("        return handle_request(m.get<Request>());\n    }\n\n")

    out.append("    void dispatch_event(const nlohmann::json& m)\n    {\n")
    out.append('        const std::string name = m.value("event", std::string{});\n')
    for e in events:
        out.append(
            f'        if (name == "{e.discriminator}")\n'
            f"            return {_handler_name(e.target_type)}(m.get<{e.target_type}>());\n"
        )
    out.append("    }\n\n")

    out.append("    void dispatch_response(const nlohmann::json& m)\n    {\n")
    out.append('        const std::string command = m.value("command", std::string{});\n')
    for e in responses:
        out.append(
            f'        if (command == "{e.discriminator}")\n'
            f"            return {_handler_name(e.target_type)}(m.get<{e.target_type}>());\n"
        )
    out.append("    }\n")
    out.append("};\n\n")

    out.append(f"}} // namespace {NAMESPACE}\n")
    return "".join(out)


def _handler_name(target_type: str) -> str:
    snake = re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", target_type)
    snake = re.sub(r"(?<=[A-Z])([A-Z][a-z])", r"_\1", snake).lower()
    return f"handle_{snake}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate C++ DAP protocol types from the DAP schema.")
    parser.add_argument("--schema", type=Path, default=SCHEMA_PATH)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    gen = Generator(schema)
    gen.generate()

    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    (output / "protocol.h").write_text(render_protocol_header(gen), encoding="utf-8")
    (output / "dap_service.h").write_text(render_service_header(gen), encoding="utf-8")

    print(
        f"Generated {len(gen.classes)} structs and {len(gen.enums)} enums into {output} "
        f"({len(gen.request_entries())} requests, {len(gen.event_entries())} events, "
        f"{len(gen.response_entries())} responses)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
