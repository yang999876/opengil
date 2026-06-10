import pathlib
import sys
import tempfile


def main() -> int:
    module_dir = pathlib.Path(sys.argv[1])
    fixture_dir = pathlib.Path(sys.argv[2])
    sys.path.insert(0, str(module_dir))

    import opengil

    doc = opengil.open(fixture_dir / "test1.gil")
    assert doc.validate()["ok"]

    created = doc.create_scene_object(
        asset_id=20001220,
        position=(1.0, 2.0, 3.0),
        rotation=(4.0, 5.0, 6.0),
        scale=(1.5, 1.5, 1.5),
    )
    assert created["kind"] == "sceneObject"
    assert created["asset_id"] == 20001220
    object_id = created["object_id"]
    assert isinstance(object_id, int)

    transformed = doc.set_scene_transform(
        object_id,
        position=(7.0, 8.0, 9.0),
        rotation=(10.0, 11.0, 12.0),
        scale=(2.0, 2.0, 2.0),
    )
    assert transformed["kind"] == "sceneTransform"
    assert transformed["object_id"] == object_id

    asset = doc.set_asset_id(object_id, 20001221)
    assert asset["kind"] == "sceneObjectAsset"
    assert asset["object_id"] == object_id
    assert asset["asset_id"] == 20001221

    model = doc.set_model_asset_id(1086324737, 20001220)
    assert model["model_asset_id"] == 20001220
    assert model["prefab_updated"]

    assert doc.validate()["ok"]
    output = pathlib.Path(tempfile.gettempdir()) / "opengil-python-bindings.gil"
    doc.save(output)

    reopened = opengil.open(output)
    assert reopened.validate()["ok"]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
