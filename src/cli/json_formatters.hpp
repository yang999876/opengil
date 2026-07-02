#pragma once

#include <string>
#include <vector>

#include "opengil/attachment_ops.hpp"
#include "opengil/custom_vars_ops.hpp"
#include "opengil/decoration_ops.hpp"
#include "opengil/model_ops.hpp"
#include "opengil/nodegraph_ops.hpp"
#include "opengil/object_ops.hpp"
#include "opengil/pixel_decoration_import_ops.hpp"
#include "opengil/prefab_ops.hpp"
#include "opengil/projectile_ops.hpp"
#include "opengil/semantic.hpp"
#include "opengil/ui_ops.hpp"
#include "opengil/ui_patch_ops.hpp"
#include "opengil/ui_structure_ops.hpp"

namespace opengil::cli {

std::string tabs_to_json(const std::vector<TabInfo>& tabs);
std::string prefabs_to_json(const std::vector<PrefabInfo>& prefabs);
std::string prefab_tabs_to_json(const std::vector<TabInfo>& tabs);
std::string model_info_to_json(const ModelInfo& info);
std::string scene_objects_to_json(const std::vector<SceneObjectInfo>& objects);
std::string nodegraphs_to_json(const std::vector<NodeGraphInfo>& graphs);

std::string set_model_summary_to_json(const SetModelSummary& summary);
std::string projectile_motion_summary_to_json(const ProjectileMotionSummary& summary);
std::string rename_prefab_summary_to_json(const RenamePrefabSummary& summary);
std::string prefab_tab_summary_to_json(const PrefabTabSummary& summary);
std::string delete_prefab_summary_to_json(const DeletePrefabSummary& summary);
std::string clone_prefab_summary_to_json(const ClonePrefabSummary& summary);
std::string copy_prefab_summary_to_json(const ClonePrefabSummary& summary);
std::string object_summary_to_json(const ObjectSummary& summary);
std::string object_color_summary_to_json(const ObjectColorSummary& summary);
std::string decoration_summary_to_json(const DecorationSummary& summary);
std::string attachment_summary_to_json(const AttachmentSummary& summary);
std::string attachment_from_decoration_summary_to_json(const AttachmentSummary& summary);
std::string attach_nodegraph_summary_to_json(const AttachNodegraphSummary& summary);
std::string attach_all_nodegraphs_summary_to_json(const AttachAllNodegraphsSummary& summary);
std::string custom_variables_list_to_json(const std::vector<PrefabCustomVariables>& rows);
std::string custom_vars_summary_to_json(const CustomVarsSummary& summary);
std::string ui_asset_list_to_json(const UiAssetList& list);
std::string ui_primitive_list_to_json(const UiPrimitiveList& list);
std::string ui_primitive_patch_summary_to_json(const UiPrimitivePatchSummary& summary);
std::string ui_structure_summary_to_json(const UiStructureSummary& summary);
std::string pixel_decoration_import_summary_to_json(const PixelDecorationImportSummary& summary);

}  // namespace opengil::cli
