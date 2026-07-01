# openGil Python API

本文从能力说明拆分而来，覆盖 Python Binding 的导入方式、常量、`GilDocument` 方法和参数约定。

## 1. Python Binding 参考

Python Binding 适合把多次修改串成一次内存态工作流，最后统一保存。

### 1.1 导入方式

```python
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path("build/Release").resolve()))

import opengil
```

或者：

```python
import opengil

doc = opengil.GilDocument.open("input.gil")
```

模块级快捷入口：

```python
doc = opengil.open("input.gil")
```

### 1.2 模块常量

```python
opengil.UI_PRIMITIVE_RECTANGLE
opengil.UI_PRIMITIVE_ELLIPSE
opengil.UI_PRIMITIVE_TRIANGLE
opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID
```

### 1.3 `GilDocument` 属性

```python
doc.path
doc.sha256
```

### 1.4 基础文件与查询方法

```python
doc.validate()
doc.save("output.gil")

doc.list_tabs()
doc.list_prefabs(tab_name=None)
doc.list_prefab_tabs(prefab_id)
doc.get_model(prefab_id)
doc.list_nodegraphs()
doc.list_scene_objects()
doc.list_preview_objects()
doc.list_ui_primitives(
    controller_entry_id=opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID
)
doc.list_custom_vars(prefab_id=None)
```

### 1.5 对象、Prefab 与模型方法

```python
doc.create_scene_object(
    asset_id,
    object_id=None,
    position=None,
    rotation=None,
    scale=None,
)

doc.create_prefab(
    asset_id,
    prefab_id=None,
    position=None,
    rotation=None,
    scale=None,
    template_path=None,
)

doc.create_scene_prefab_instance(
    prefab_id,
    asset_id,
    object_id=None,
    position=None,
    rotation=None,
    scale=None,
    template_path=None,
)

doc.set_scene_transform(object_id, position=None, rotation=None, scale=None)
doc.set_preview_transform(object_id, position=None, rotation=None, scale=None)

doc.set_scene_object_asset_id(object_id, asset_id)
doc.set_asset_id(object_id, asset_id)

doc.set_model_asset_id(prefab_id, asset_id)
doc.set_prefab_model_asset_id(prefab_id, asset_id)
doc.set_empty_model(prefab_id)
doc.rename_prefab(prefab_id, name)
doc.delete_prefab(prefab_id)
```

### 1.6 Prefab 克隆与复制方法

```python
doc.clone_prefab_into_tab(
    source_prefab_id,
    target_tab_name,
    new_prefab_name,
    new_prefab_id=None,
    prefab_id_start_after=None,
    preview_x_step=1.240311,
    preview_z_step=2.238042,
)

doc.clone_prefab_into_tab_by_id(
    source_prefab_id,
    target_tab_id,
    new_prefab_name,
    new_prefab_id=None,
    prefab_id_start_after=None,
    preview_x_step=1.240311,
    preview_z_step=2.238042,
)

doc.copy_prefab_to_tab(
    source_prefab_id,
    target_tab_name,
    new_prefab_name=None,
    new_prefab_id=None,
    prefab_id_start_after=None,
    preview_x_step=1.240311,
    preview_z_step=2.238042,
)

doc.copy_prefab_to_tab_by_id(
    source_prefab_id,
    target_tab_id,
    new_prefab_name=None,
    new_prefab_id=None,
    prefab_id_start_after=None,
    preview_x_step=1.240311,
    preview_z_step=2.238042,
)
```

### 1.7 NodeGraph、Projectile 与 Custom Vars 方法

```python
doc.attach_nodegraph(prefab_id, nodegraph_id)
doc.attach_all_nodegraphs(prefab_id)

doc.set_projectile_motion(prefab_id, x, y, gravity=None)
doc.set_projectile_motion_from_angle(prefab_id, angle_deg, speed, gravity=None)

doc.add_custom_var(prefab_id, name, type)
doc.remove_custom_var(prefab_id, name)
doc.copy_custom_vars(source_prefab_id, target_prefab_id)
doc.sync_tab_custom_vars(source_prefab_id, tab_name)
doc.sync_tab_custom_vars_by_tab_id(source_prefab_id, tab_id)
```

### 1.8 Decoration、Attachment 与 UI 方法

```python
doc.add_decorations(prefab_id, specs)
doc.add_attachment_points(prefab_id, specs, object_id=None)
doc.add_attachment_points_from_decorations(prefab_id, object_id=None)

doc.set_ui_primitive_type(
    primitive_index,
    primitive_type_id,
    controller_entry_id=opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID,
)

doc.set_ui_primitive_color(
    primitive_index,
    color,
    controller_entry_id=opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID,
)

doc.set_ui_primitive_transform(
    primitive_index,
    transform,
    controller_entry_id=opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID,
)

doc.set_ui_primitive_layer(
    primitive_index,
    layer,
    controller_entry_id=opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID,
)

doc.set_ui_primitive_name(
    primitive_index,
    name,
    controller_entry_id=opengil.DEFAULT_UI_PRIMITIVE_CONTROLLER_ENTRY_ID,
)

doc.delete_ui_primitives(
    primitive_indexes,
    target_controller_entry_id=None,
)

doc.import_pixel_png_as_ui_primitives(png_path, pixel_size)
doc.import_pixel_png_as_decoration_prefab(
    png_path,
    prefab_id,
    asset_id,
    pixel_size,
)
```

### 1.9 Python 参数约定

```python
position = (1.0, 2.0, 3.0)
rotation = (0.0, 90.0, 0.0)
scale = (1.0, 1.0, 1.0)

ui_transform = {
    "position": (10.0, 20.0),
    "size": (80.0, 80.0),
    "scale": (1.0, 1.0, 1.0),
    "rotation_z": 0.0,
}

decoration_specs = [
    {
        "asset_id": 20001220,
        "name": "DecoA",
        "position": (0.0, 1.0, 0.0),
        "rotation": (0.0, 0.0, 0.0),
        "scale": (1.0, 1.0, 1.0),
    }
]

attachment_specs = [
    {
        "name": "Hand",
        "display_name": "Hand Point",
        "x": 0.48,
        "y": 1.52,
        "rot_x": -37.9,
        "rot_y": 81.9,
    }
]
```

### 1.10 Python 最小示例

```python
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path("build/Release").resolve()))

import opengil

doc = opengil.open("input.gil")

print(doc.validate())
print(doc.list_tabs())
print(doc.list_prefabs())

doc.set_model_asset_id(1077936130, 20001220)
doc.save("output.gil")
```
