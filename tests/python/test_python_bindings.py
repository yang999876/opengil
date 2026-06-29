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

    expected_methods = [
        "add_attachment_points",
        "add_attachment_points_from_decorations",
        "add_custom_var",
        "add_decorations",
        "attach_all_nodegraphs",
        "attach_nodegraph",
        "clone_prefab_into_tab",
        "clone_prefab_into_tab_by_id",
        "copy_custom_vars",
        "copy_prefab_to_tab",
        "copy_prefab_to_tab_by_id",
        "create_prefab",
        "create_scene_object",
        "create_scene_prefab_instance",
        "delete_prefab",
        "get_model",
        "import_pixel_png_as_ui_primitives",
        "import_pixel_png_as_decoration_prefab",
        "list_custom_vars",
        "list_nodegraphs",
        "list_prefab_tabs",
        "list_prefabs",
        "list_preview_objects",
        "list_scene_objects",
        "list_tabs",
        "list_ui_primitives",
        "remove_custom_var",
        "rename_prefab",
        "delete_ui_primitives",
        "save",
        "set_asset_id",
        "set_empty_model",
        "set_model_asset_id",
        "set_prefab_model_asset_id",
        "set_preview_transform",
        "set_projectile_motion",
        "set_projectile_motion_from_angle",
        "set_scene_object_asset_id",
        "set_scene_transform",
        "set_ui_primitive_color",
        "set_ui_primitive_layer",
        "set_ui_primitive_name",
        "set_ui_primitive_transform",
        "set_ui_primitive_type",
        "sync_tab_custom_vars",
        "sync_tab_custom_vars_by_tab_id",
        "validate",
    ]
    for name in expected_methods:
        assert callable(getattr(doc, name)), name

    assert isinstance(doc.list_tabs(), list)
    assert isinstance(doc.list_prefabs(), list)
    assert isinstance(doc.list_nodegraphs(), list)
    assert isinstance(doc.list_preview_objects(), list)
    assert isinstance(doc.list_scene_objects(), list)
    assert isinstance(doc.list_custom_vars(), list)
    assert isinstance(doc.list_ui_primitives(), dict)
    assert doc.get_model(1086324737)["prefab_id"] == 1086324737

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
