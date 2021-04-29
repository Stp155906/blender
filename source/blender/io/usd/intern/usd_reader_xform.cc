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
 */

#include "usd_reader_xform.h"
#include "usd_reader_prim.h"

extern "C" {
#include "DNA_cachefile_types.h"
#include "DNA_constraint_types.h"
#include "DNA_modifier_types.h"
#include "DNA_space_types.h" /* for FILE_MAX */

#include "BKE_constraint.h"
#include "BKE_lib_id.h"
#include "BKE_library.h"
#include "BKE_modifier.h"
#include "BKE_object.h"

#include "BLI_listbase.h"
#include "BLI_math_geom.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "WM_api.h"
#include "WM_types.h"
}

#include <pxr/base/gf/math.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/pxr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>

namespace blender::io::usd {

void USDXformReader::create_object(Main *bmain, const double /* motionSampleTime */)
{
  object_ = BKE_object_add_only_object(bmain, OB_EMPTY, name_.c_str());
  object_->empty_drawsize = 0.1f;
  object_->data = NULL;
}

void USDXformReader::read_object_data(Main * /* bmain */, const double motionSampleTime)
{
  bool is_constant;
  float transform_from_usd[4][4];

  read_matrix(transform_from_usd, motionSampleTime, import_params_.scale, is_constant);

  if (!is_constant) {
    bConstraint *con = BKE_constraint_add_for_object(
        object_, NULL, CONSTRAINT_TYPE_TRANSFORM_CACHE);
    bTransformCacheConstraint *data = static_cast<bTransformCacheConstraint *>(con->data);

    std::string prim_path = use_parent_xform_ ? prim_.GetParent().GetPath().GetAsString() :
                                                prim_path_;

    BLI_strncpy(data->object_path, prim_path.c_str(), FILE_MAX);

    data->cache_file = settings_->cache_file;
    id_us_plus(&data->cache_file->id);
  }

  BKE_object_apply_mat4(object_, transform_from_usd, true, false);
}

void USDXformReader::read_matrix(float r_mat[4][4] /* local matrix */,
                                 const float time,
                                 const float scale,
                                 bool &is_constant)
{
  is_constant = true;
  unit_m4(r_mat);

  pxr::UsdGeomXformable xformable;

  if (use_parent_xform_) {
    xformable = pxr::UsdGeomXformable(prim_.GetParent());
  }
  else {
    xformable = pxr::UsdGeomXformable(prim_);
  }

  if (!xformable) {
    // This might happen if the prim is a Scope.
    return;
  }

  is_constant = !xformable.TransformMightBeTimeVarying();

  pxr::GfMatrix4d usd_local_xf;
  bool reset_xform_stack;
  xformable.GetLocalTransformation(&usd_local_xf, &reset_xform_stack, time);

  // Convert the result to a float matrix.
  pxr::GfMatrix4f mat4f = pxr::GfMatrix4f(usd_local_xf);
  mat4f.Get(r_mat);

  /* Apply global scaling and rotation only to root objects, parenting
   * will propagate it. */
  if ((scale != 1.0 || settings_->do_convert_mat) && is_root_xform_object()) {

    if (scale != 1.0f) {
      float scale_mat[4][4];
      scale_m4_fl(scale_mat, scale);
      mul_m4_m4m4(r_mat, scale_mat, r_mat);
    }

    if (settings_->do_convert_mat) {
      mul_m4_m4m4(r_mat, settings_->conversion_mat, r_mat);
    }
  }
}

bool USDXformReader::prim_has_xform_ops() const
{
  pxr::UsdGeomXformable xformable(prim_);

  if (!xformable) {
    // This might happen if the prim is a Scope.
    return false;
  }

  bool reset_xform_stack = false;

  return !xformable.GetOrderedXformOps(&reset_xform_stack).empty();
}

bool USDXformReader::is_root_xform_object() const
{
  // It's not sufficient to check for a null parent to determine
  // if the current object is the root, because the parent could
  // represent a scope, which is not xformable.  E.g., an Xform
  // parented to a single Scope would be considered the root.

  if (prim_.IsInMaster()) {
    // We don't consider prototypes to be root prims,
    // because we never want to apply global scaling
    // or rotations to the ptototypes themselves.
    return false;
  }

  if (prim_.IsA<pxr::UsdGeomXformable>()) {
    // If we don't have an ancestor that also wraps
    // UsdGeomXformable, then we are the root.
    const USDPrimReader *cur_parent = parent_reader_;

    while (cur_parent) {
      if (cur_parent->prim().IsA<pxr::UsdGeomXformable>()) {
        return false;
      }
      cur_parent = cur_parent->parent();
    }

    if (!cur_parent) {
      // No ancestor prim was an xformable, so we
      // are the root.
      return true;
    }
  }

  return false;
}

}  // namespace blender::io::usd
