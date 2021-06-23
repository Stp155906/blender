/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editor/io
 */

#ifdef WITH_USD
#  include "DNA_modifier_types.h"
#  include "DNA_space_types.h"
#  include <string.h>

#  include "BKE_context.h"
#  include "BKE_main.h"
#  include "BKE_report.h"

#  include "BLI_blenlib.h"
#  include "BLI_path_util.h"
#  include "BLI_string.h"
#  include "BLI_utildefines.h"

#  include "BLT_translation.h"

#  include "ED_object.h"

#  include "MEM_guardedalloc.h"

#  include "RNA_access.h"
#  include "RNA_define.h"

#  include "RNA_enum_types.h"

#  include "UI_interface.h"
#  include "UI_resources.h"

#  include "WM_api.h"
#  include "WM_types.h"

#  include "DEG_depsgraph.h"

#  include "io_usd.h"
#  include "usd.h"

#  include "stdio.h"

const EnumPropertyItem rna_enum_usd_export_evaluation_mode_items[] = {
    {DAG_EVAL_RENDER,
     "RENDER",
     0,
     "Render",
     "Use Render settings for object visibility, modifier settings, etc"},
    {DAG_EVAL_VIEWPORT,
     "VIEWPORT",
     0,
     "Viewport",
     "Use Viewport settings for object visibility, modifier settings, etc"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropertyItem rna_enum_usd_import_read_flags[] = {
    {MOD_MESHSEQ_READ_VERT, "VERT", 0, "Vertex", ""},
    {MOD_MESHSEQ_READ_POLY, "POLY", 0, "Faces", ""},
    {MOD_MESHSEQ_READ_UV, "UV", 0, "UV", ""},
    {MOD_MESHSEQ_READ_COLOR, "COLOR", 0, "Color", ""},
    {0, NULL, 0, NULL, NULL},
};

/* Stored in the wmOperator's customdata field to indicate it should run as a background job.
 * This is set when the operator is invoked, and not set when it is only executed. */
enum { AS_BACKGROUND_JOB = 1 };
typedef struct eUSDOperatorOptions {
  bool as_background_job;
} eUSDOperatorOptions;

static int wm_usd_export_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  eUSDOperatorOptions *options = MEM_callocN(sizeof(eUSDOperatorOptions), "eUSDOperatorOptions");
  options->as_background_job = true;
  op->customdata = options;

  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    Main *bmain = CTX_data_main(C);
    char filepath[FILE_MAX];
    const char *main_blendfile_path = BKE_main_blendfile_path(bmain);

    if (main_blendfile_path[0] == '\0') {
      BLI_strncpy(filepath, "untitled", sizeof(filepath));
    }
    else {
      BLI_strncpy(filepath, main_blendfile_path, sizeof(filepath));
    }

    BLI_path_extension_replace(filepath, sizeof(filepath), ".usdc");
    RNA_string_set(op->ptr, "filepath", filepath);
  }

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static int wm_usd_export_exec(bContext *C, wmOperator *op)
{
  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  eUSDOperatorOptions *options = (eUSDOperatorOptions *)op->customdata;
  const bool as_background_job = (options != NULL && options->as_background_job);
  MEM_SAFE_FREE(op->customdata);

  const bool selected_objects_only = RNA_boolean_get(op->ptr, "selected_objects_only");
  const bool visible_objects_only = RNA_boolean_get(op->ptr, "visible_objects_only");
  const bool export_animation = RNA_boolean_get(op->ptr, "export_animation");
  const bool export_hair = RNA_boolean_get(op->ptr, "export_hair");
  const bool export_uvmaps = RNA_boolean_get(op->ptr, "export_uvmaps");
  const bool export_normals = RNA_boolean_get(op->ptr, "export_normals");
  const bool export_materials = RNA_boolean_get(op->ptr, "export_materials");
  const bool use_instancing = RNA_boolean_get(op->ptr, "use_instancing");
  const bool evaluation_mode = RNA_enum_get(op->ptr, "evaluation_mode");

  struct USDExportParams params = {
      export_animation,
      export_hair,
      export_uvmaps,
      export_normals,
      export_materials,
      selected_objects_only,
      visible_objects_only,
      use_instancing,
      evaluation_mode,
  };

  bool ok = USD_export(C, filename, &params, as_background_job);

  return as_background_job || ok ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void wm_usd_export_draw(bContext *UNUSED(C), wmOperator *op)
{
  uiLayout *layout = op->layout;
  uiLayout *col;
  struct PointerRNA *ptr = op->ptr;

  uiLayoutSetPropSep(layout, true);

  uiLayout *box = uiLayoutBox(layout);

  col = uiLayoutColumn(box, true);
  uiItemR(col, ptr, "selected_objects_only", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "visible_objects_only", 0, NULL, ICON_NONE);

  col = uiLayoutColumn(box, true);
  uiItemR(col, ptr, "export_animation", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_hair", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_uvmaps", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_normals", 0, NULL, ICON_NONE);
  uiItemR(col, ptr, "export_materials", 0, NULL, ICON_NONE);

  col = uiLayoutColumn(box, true);
  uiItemR(col, ptr, "evaluation_mode", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Experimental"), ICON_NONE);
  uiItemR(box, ptr, "use_instancing", 0, NULL, ICON_NONE);
}

void WM_OT_usd_export(struct wmOperatorType *ot)
{
  ot->name = "Export USD";
  ot->description = "Export current scene in a USD archive";
  ot->idname = "WM_OT_usd_export";

  ot->invoke = wm_usd_export_invoke;
  ot->exec = wm_usd_export_exec;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_usd_export_draw;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_USD,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);

  RNA_def_boolean(ot->srna,
                  "selected_objects_only",
                  false,
                  "Selection Only",
                  "Only selected objects are exported. Unselected parents of selected objects are "
                  "exported as empty transform");

  RNA_def_boolean(ot->srna,
                  "visible_objects_only",
                  true,
                  "Visible Only",
                  "Only visible objects are exported. Invisible parents of exported objects are "
                  "exported as empty transform");

  RNA_def_boolean(ot->srna,
                  "export_animation",
                  false,
                  "Animation",
                  "When checked, the render frame range is exported. When false, only the current "
                  "frame is exported");
  RNA_def_boolean(
      ot->srna, "export_hair", false, "Hair", "When checked, hair is exported as USD curves");
  RNA_def_boolean(ot->srna,
                  "export_uvmaps",
                  true,
                  "UV Maps",
                  "When checked, all UV maps of exported meshes are included in the export");
  RNA_def_boolean(ot->srna,
                  "export_normals",
                  true,
                  "Normals",
                  "When checked, normals of exported meshes are included in the export");
  RNA_def_boolean(ot->srna,
                  "export_materials",
                  true,
                  "Materials",
                  "When checked, the viewport settings of materials are exported as USD preview "
                  "materials, and material assignments are exported as geometry subsets");

  RNA_def_boolean(ot->srna,
                  "use_instancing",
                  false,
                  "Instancing",
                  "When checked, instanced objects are exported as references in USD. "
                  "When unchecked, instanced objects are exported as real objects");

  RNA_def_enum(ot->srna,
               "evaluation_mode",
               rna_enum_usd_export_evaluation_mode_items,
               DAG_EVAL_RENDER,
               "Use Settings for",
               "Determines visibility of objects, modifier settings, and other areas where there "
               "are different settings for viewport and rendering");
}

/* ====== USD Import ====== */

static int wm_usd_import_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  eUSDOperatorOptions *options = MEM_callocN(sizeof(eUSDOperatorOptions), "eUSDOperatorOptions");
  options->as_background_job = true;
  op->customdata = options;

  return WM_operator_filesel(C, op, event);
}

static int wm_usd_import_exec(bContext *C, wmOperator *op)
{
  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  eUSDOperatorOptions *options = (eUSDOperatorOptions *)op->customdata;
  const bool as_background_job = (options != NULL && options->as_background_job);
  MEM_SAFE_FREE(op->customdata);

  const float scale = RNA_float_get(op->ptr, "scale");
  const bool apply_unit_conversion_scale = RNA_boolean_get(op->ptr, "apply_unit_conversion_scale");

  const bool set_frame_range = RNA_boolean_get(op->ptr, "set_frame_range");
  const char global_read_flag = RNA_enum_get(op->ptr, "global_read_flag");
  const bool import_cameras = RNA_boolean_get(op->ptr, "import_cameras");
  const bool import_curves = RNA_boolean_get(op->ptr, "import_curves");
  const bool import_lights = RNA_boolean_get(op->ptr, "import_lights");
  const bool import_materials = RNA_boolean_get(op->ptr, "import_materials");
  const bool import_meshes = RNA_boolean_get(op->ptr, "import_meshes");
  const bool import_volumes = RNA_boolean_get(op->ptr, "import_volumes");

  const bool import_subdiv = RNA_boolean_get(op->ptr, "import_subdiv");

  const bool import_instance_proxies = RNA_boolean_get(op->ptr, "import_instance_proxies");

  const bool import_visible_only = RNA_boolean_get(op->ptr, "import_visible_only");

  const bool create_collection = RNA_boolean_get(op->ptr, "create_collection");

  char *prim_path_mask = malloc(1024);
  RNA_string_get(op->ptr, "prim_path_mask", prim_path_mask);

  const bool import_guide = RNA_boolean_get(op->ptr, "import_guide");
  const bool import_proxy = RNA_boolean_get(op->ptr, "import_proxy");
  const bool import_render = RNA_boolean_get(op->ptr, "import_render");

  const bool use_instancing = RNA_boolean_get(op->ptr, "use_instancing");

  const bool import_usd_preview = RNA_boolean_get(op->ptr, "import_usd_preview");
  const bool set_material_blend = RNA_boolean_get(op->ptr, "set_material_blend");

  const float light_intensity_scale = RNA_float_get(op->ptr, "light_intensity_scale");

  const bool convert_light_from_nits = RNA_boolean_get(op->ptr, "convert_light_from_nits");

  const bool scale_light_radius = RNA_boolean_get(op->ptr, "scale_light_radius");

  const bool create_background_shader = RNA_boolean_get(op->ptr, "create_background_shader");

  /* TODO(makowalski): Add support for sequences. */
  const bool is_sequence = false;
  int offset = 0;
  int sequence_len = 1;

  /* Switch out of edit mode to avoid being stuck in it (T54326). */
  Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    ED_object_mode_set(C, OB_MODE_EDIT);
  }

  const bool validate_meshes = false;

  struct USDImportParams params = {scale,
                                   is_sequence,
                                   set_frame_range,
                                   sequence_len,
                                   offset,
                                   validate_meshes,
                                   global_read_flag,
                                   import_cameras,
                                   import_curves,
                                   import_lights,
                                   import_materials,
                                   import_meshes,
                                   import_volumes,
                                   prim_path_mask,
                                   import_subdiv,
                                   import_instance_proxies,
                                   create_collection,
                                   import_guide,
                                   import_proxy,
                                   import_render,
                                   import_visible_only,
                                   use_instancing,
                                   import_usd_preview,
                                   set_material_blend,
                                   light_intensity_scale,
                                   apply_unit_conversion_scale,
                                   convert_light_from_nits,
                                   scale_light_radius,
                                   create_background_shader};

  const bool ok = USD_import(C, filename, &params, as_background_job);

  return as_background_job || ok ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

static void wm_usd_import_draw(bContext *UNUSED(C), wmOperator *op)
{
  uiLayout *layout = op->layout;
  struct PointerRNA *ptr = op->ptr;

  uiLayoutSetPropSep(layout, true);

  uiLayout *box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("USD Import"), ICON_NONE);
  uiItemL(box, IFACE_("Global Read Flag:"), ICON_NONE);
  uiItemR(box, ptr, "global_read_flag", UI_ITEM_R_EXPAND, NULL, ICON_NONE);
  uiItemL(box, IFACE_("Manual Transform:"), ICON_NONE);
  uiItemR(box, ptr, "scale", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "apply_unit_conversion_scale", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Options:"), ICON_NONE);
  uiItemR(box, ptr, "relative_path", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "set_frame_range", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_subdiv", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_instance_proxies", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_visible_only", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "create_collection", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "light_intensity_scale", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "convert_light_from_nits", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "scale_light_radius", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "create_background_shader", 0, NULL, ICON_NONE);

  uiLayout *prim_path_mask_box = uiLayoutBox(box);
  uiItemL(prim_path_mask_box, IFACE_("Prim Path Mask:"), ICON_NONE);
  uiItemR(prim_path_mask_box, ptr, "prim_path_mask", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Primitive Types:"), ICON_OBJECT_DATA);
  uiItemR(box, ptr, "import_cameras", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_curves", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_lights", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_materials", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_meshes", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_volumes", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Purpose"), ICON_NONE);
  uiItemR(box, ptr, "import_guide", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_proxy", 0, NULL, ICON_NONE);
  uiItemR(box, ptr, "import_render", 0, NULL, ICON_NONE);

  box = uiLayoutBox(layout);
  uiItemL(box, IFACE_("Experimental"), ICON_NONE);
  uiItemR(box, ptr, "use_instancing", 0, NULL, ICON_NONE);
  if (RNA_boolean_get(ptr, "import_materials")) {
    uiItemR(box, ptr, "import_usd_preview", 0, NULL, ICON_NONE);
    if (RNA_boolean_get(ptr, "import_usd_preview")) {
      uiItemR(box, ptr, "set_material_blend", 0, NULL, ICON_NONE);
    }
  }
}

void WM_OT_usd_import(struct wmOperatorType *ot)
{
  PropertyRNA *prop;

  ot->name = "Import USD";
  ot->description = "Import USD stage into current scene";
  ot->idname = "WM_OT_usd_import";

  ot->invoke = wm_usd_import_invoke;
  ot->exec = wm_usd_import_exec;
  ot->poll = WM_operator_winactive;
  ot->ui = wm_usd_import_draw;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_USD,
                                 FILE_BLENDER,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_RELPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_ALPHA);

  RNA_def_float(
      ot->srna,
      "scale",
      1.0f,
      0.0001f,
      1000.0f,
      "Scale",
      "Value by which to enlarge or shrink the objects with respect to the world's origin. "
      "This scaling is applied in addition to the Stage's meters-per-unit scaling value if "
      "the Apply Unit Conversion Scale option is enabled",
      0.0001f,
      1000.0f);

  RNA_def_boolean(
      ot->srna,
      "apply_unit_conversion_scale",
      true,
      "Apply Unit Conversion Scale",
      "Scale the scene objects by the USD stage's meters-per-unit value. "
      "This scaling is applied in addition to the value specified in the Scale option");

  RNA_def_boolean(ot->srna,
                  "set_frame_range",
                  true,
                  "Set Frame Range",
                  "Update scene's start and end frame to match those of the USD archive");

  RNA_def_boolean(ot->srna, "import_cameras", true, "Import Cameras", "");
  RNA_def_boolean(ot->srna, "import_curves", true, "Import Curves", "");
  RNA_def_boolean(ot->srna, "import_lights", true, "Import Lights", "");
  RNA_def_boolean(ot->srna, "import_materials", true, "Import Materials", "");
  RNA_def_boolean(ot->srna, "import_meshes", true, "Import Meshes", "");
  RNA_def_boolean(ot->srna, "import_volumes", true, "Import Volumes", "");

  RNA_def_boolean(ot->srna,
                  "import_subdiv",
                  false,
                  "Import Subdiv Scheme",
                  "Subdiv surface modifiers will be created based on USD "
                  "SubdivisionScheme attribute");

  RNA_def_boolean(ot->srna,
                  "import_instance_proxies",
                  true,
                  "Import Instance Proxies",
                  "Create unique Blender objects for USD instances");

  RNA_def_boolean(ot->srna,
                  "import_visible_only",
                  true,
                  "Visible Primitives Only",
                  "Do not import invisible USD primitives. "
                  "Only applies to primitives with a non-animating visibility attribute. "
                  "Primitives with animating visibility will always be imported");

  RNA_def_boolean(ot->srna,
                  "create_collection",
                  false,
                  "Create Collection",
                  "Add all imported objects to a new collection");

  prop = RNA_def_enum(ot->srna,
                      "global_read_flag",
                      rna_enum_usd_import_read_flags,
                      0,
                      "Flags",
                      "Set read flag for all USD import mesh sequence cache modifiers");

  /* Specify that the flag contains multiple enums. */
  RNA_def_property_flag(prop, PROP_ENUM_FLAG);
  /* Set the flag bits enabled by default. */
  RNA_def_property_enum_default(
      prop, (MOD_MESHSEQ_READ_VERT | MOD_MESHSEQ_READ_POLY | MOD_MESHSEQ_READ_UV));

  RNA_def_string(ot->srna,
                 "prim_path_mask",
                 NULL,
                 1024,
                 "",
                 "If set, specifies the path of the USD primitive to load from the stage");

  RNA_def_boolean(ot->srna, "import_guide", false, "Guide", "Import guide geometry");

  RNA_def_boolean(ot->srna, "import_proxy", true, "Proxy", "Import proxy geometry");

  RNA_def_boolean(ot->srna, "import_render", true, "Render", "Import final render geometry");

  RNA_def_boolean(ot->srna,
                  "use_instancing",
                  false,
                  "Instancing",
                  "Import USD scenegraph instances as Blender collection instances. "
                  "Note that point instancers are not yet handled by this option");

  RNA_def_boolean(ot->srna,
                  "import_usd_preview",
                  false,
                  "Import USD Preview",
                  "Convert UsdPreviewSurface shaders to Principled BSD shader networks");

  RNA_def_boolean(ot->srna,
                  "set_material_blend",
                  true,
                  "Set Material Blend",
                  "If the Import USD Preview option is enabled, "
                  "the material blend method will automatically be set based on the "
                  "shader's opacity and opacityThreshold inputs");

  RNA_def_float(ot->srna,
                "light_intensity_scale",
                1.0f,
                0.0001f,
                10000.0f,
                "Light Intensity Scale",
                "Value by which to scale the intensity of imported lights",
                0.0001f,
                1000.0f);

  RNA_def_boolean(ot->srna,
                  "convert_light_from_nits",
                  false,
                  "Convert Light Units from Nits",
                  "Convert light intensity units from nits");

  RNA_def_boolean(ot->srna,
                  "scale_light_radius",
                  false,
                  "Scale Light Radius",
                  "Apply the scene scale factor (from unit conversion or manual scaling) "
                  "to the radius size of spot and local lights");

  RNA_def_boolean(ot->srna,
                  "create_background_shader",
                  true,
                  "Create Background Shader",
                  "Convert USD dome lights to world background shaders");
}

#endif /* WITH_USD */
