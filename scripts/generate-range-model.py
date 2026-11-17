"""Rebuild the original demo rifle asset using only Python's standard library."""
import base64
import json
import struct
from pathlib import Path


def generate():
    data = bytearray()
    views, accessors = [], []

    def accessor(rows, shape, component=5126, bounds=False):
        while len(data) % 4:
            data.append(0)
        start = len(data)
        code = "f" if component == 5126 else "H"
        for row in rows:
            data.extend(struct.pack("<" + code * len(row), *row))
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(data) - start})
        item = {"bufferView": len(views) - 1, "componentType": component, "count": len(rows), "type": shape}
        if bounds:
            item["min"] = [min(row[i] for row in rows) for i in range(len(rows[0]))]
            item["max"] = [max(row[i] for row in rows) for i in range(len(rows[0]))]
        accessors.append(item)
        return len(accessors) - 1

    # Centre, dimensions, material, joint. +Z is the muzzle direction.
    boxes = [
        ((0, 0, 0), (0.11, 0.1, 0.4), 0, 0),
        ((0, -0.1, -0.06), (0.09, 0.17, 0.1), 1, 0),
        ((0, 0.04, 0.02), (0.085, 0.025, 0.42), 2, 0),
        ((0, 0, 0.30), (0.055, 0.055, 0.2), 1, 0),
        ((0, 0.06, 0.1), (0.004, 0.01, 0.01), 3, 0),
        ((0, -0.12, 0.06), (0.07, 0.19, 0.1), 1, 1),
        ((0, 0, 0.17), (0.12, 0.11, 0.045), 3, 0),
    ]
    primitives = []
    for material in range(4):
        positions, normals, joints, weights = [], [], [], []
        for centre, size, colour, joint in boxes:
            if colour != material:
                continue
            for axis in range(3):
                u, v = (axis + 1) % 3, (axis + 2) % 3
                for sign in (-1, 1):
                    corners = []
                    for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                        p = list(centre)
                        p[axis] += sign * size[axis] / 2
                        p[u] += a * size[u] / 2
                        p[v] += b * size[v] / 2
                        corners.append(p)
                    order = (0, 1, 2, 0, 2, 3) if sign == 1 else (0, 2, 1, 0, 3, 2)
                    for i in order:
                        positions.append(corners[i])
                        normal = [0, 0, 0]
                        normal[axis] = sign
                        normals.append(normal)
                        joints.append((joint, 0, 0, 0))
                        weights.append((1, 0, 0, 0))
        primitives.append({"attributes": {
            "POSITION": accessor(positions, "VEC3", bounds=True),
            "NORMAL": accessor(normals, "VEC3"),
            "JOINTS_0": accessor(joints, "VEC4", 5123),
            "WEIGHTS_0": accessor(weights, "VEC4")}, "material": material})
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    magazine_inverse = identity.copy()
    magazine_inverse[13], magazine_inverse[14] = 0.12, -0.06
    inverse_bind = accessor([identity, magazine_inverse], "MAT4")
    times = accessor([(0,), (0.25,), (0.65,), (1,)], "SCALAR", bounds=True)
    translations = accessor([(0, -0.12, 0.06), (0, -0.29, 0.06), (0, -0.29, 0.06), (0, -0.12, 0.06)], "VEC3")
    colours = [[0.255, 0.31, 0.34, 1], [0.11, 0.15, 0.19, 1], [0.42, 0.5, 0.53, 1], [1, 0.67, 0.31, 1]]
    model = {
        "asset": {"version": "2.0", "generator": "Weapon Library demo asset generator"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "Rifle", "mesh": 0, "skin": 0},
                  {"name": "WeaponRoot", "children": [2]},
                  {"name": "Magazine", "translation": [0, -0.12, 0.06]}],
        "skins": [{"joints": [1, 2], "skeleton": 1, "inverseBindMatrices": inverse_bind}],
        "meshes": [{"primitives": primitives}],
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": c, "metallicFactor": 0, "roughnessFactor": 0.8}}
                      for c in colours],
        "animations": [{"name": "Reload", "samplers": [{"input": times, "output": translations, "interpolation": "LINEAR"}],
                        "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}}]}],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
        "bufferViews": views, "accessors": accessors,
    }
    output = Path(__file__).resolve().parent.parent / "examples/range/visuals/models/rifle.gltf"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate()
