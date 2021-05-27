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
#include "usd_writer_light.h"
#include "usd_light_convert.h"
#include "usd_hierarchy_iterator.h"

#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>

#include "BLI_assert.h"
#include "BLI_math.h"
#include "BLI_math_matrix.h"
#include "BLI_utildefines.h"

#include "DNA_light_types.h"
#include "DNA_object_types.h"

#include "WM_api.h"
#include "WM_types.h"

namespace usdtokens {
// Attribute names.
static const pxr::TfToken angle("angle", pxr::TfToken::Immortal);
static const pxr::TfToken color("color", pxr::TfToken::Immortal);
static const pxr::TfToken height("height", pxr::TfToken::Immortal);
static const pxr::TfToken intensity("intensity", pxr::TfToken::Immortal);
static const pxr::TfToken radius("radius", pxr::TfToken::Immortal);
static const pxr::TfToken specular("specular", pxr::TfToken::Immortal);
static const pxr::TfToken width("width", pxr::TfToken::Immortal);
}  // namespace usdtokens

namespace blender::io::usd {

USDLightWriter::USDLightWriter(const USDExporterContext &ctx) : USDAbstractWriter(ctx)
{
}

bool USDLightWriter::is_supported(const HierarchyContext *context) const
{
  Light *light = static_cast<Light *>(context->object->data);
  return ELEM(light->type, LA_AREA, LA_LOCAL, LA_SUN, LA_SPOT);
}

void USDLightWriter::do_write(HierarchyContext &context)
{
  pxr::UsdStageRefPtr stage = usd_export_context_.stage;
  pxr::UsdTimeCode timecode = get_export_time_code();

  /* Need to account for the scene scale when converting to nits
   * or scaling the radius. */
  float world_scale = mat4_to_scale(context.matrix_world);

  float radius_scale = usd_export_context_.export_params.scale_light_radius ? 1.0f / world_scale : 1.0f;

  Light *light = static_cast<Light *>(context.object->data);
  pxr::UsdLuxLight usd_light;

  switch (light->type) {
    case LA_AREA:
      switch (light->area_shape) {
        case LA_AREA_DISK:
        case LA_AREA_ELLIPSE: { /* An ellipse light will deteriorate into a disk light. */
          pxr::UsdLuxDiskLight disk_light =
              (usd_export_context_.export_params.export_as_overs) ?
                  pxr::UsdLuxDiskLight(
                      usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path)) :
                  pxr::UsdLuxDiskLight::Define(usd_export_context_.stage,
                                               usd_export_context_.usd_path);
          disk_light.CreateRadiusAttr().Set(light->area_size / 2.0f, timecode);

          if (usd_export_context_.export_params.backward_compatible) {
            if (pxr::UsdAttribute attr = disk_light.GetPrim().CreateAttribute(
                    usdtokens::radius, pxr::SdfValueTypeNames->Float, true)) {
              attr.Set(light->area_size / 2.0f, timecode);
            }
          }

          usd_light = disk_light;
          break;
        }
        case LA_AREA_RECT: {
          pxr::UsdLuxRectLight rect_light =
              (usd_export_context_.export_params.export_as_overs) ?
                  pxr::UsdLuxRectLight(
                      usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path)) :
                  pxr::UsdLuxRectLight::Define(usd_export_context_.stage,
                                               usd_export_context_.usd_path);
          rect_light.CreateWidthAttr().Set(light->area_size, timecode);
          rect_light.CreateHeightAttr().Set(light->area_sizey, timecode);

          if (usd_export_context_.export_params.backward_compatible) {
            pxr::UsdAttribute attr = rect_light.GetPrim().CreateAttribute(
                usdtokens::width, pxr::SdfValueTypeNames->Float, true);
            if (attr) {
              attr.Set(light->area_size, timecode);
            }
            attr = rect_light.GetPrim().CreateAttribute(
                usdtokens::height, pxr::SdfValueTypeNames->Float, true);
            if (attr) {
              attr.Set(light->area_sizey, timecode);
            }
          }

          usd_light = rect_light;
          break;
        }
        case LA_AREA_SQUARE: {
          pxr::UsdLuxRectLight rect_light =
              (usd_export_context_.export_params.export_as_overs) ?
                  pxr::UsdLuxRectLight(
                      usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path)) :
                  pxr::UsdLuxRectLight::Define(usd_export_context_.stage,
                                               usd_export_context_.usd_path);
          rect_light.CreateWidthAttr().Set(light->area_size, timecode);
          rect_light.CreateHeightAttr().Set(light->area_size, timecode);

          if (usd_export_context_.export_params.backward_compatible) {
            pxr::UsdAttribute attr = rect_light.GetPrim().CreateAttribute(
                usdtokens::width, pxr::SdfValueTypeNames->Float, true);
            if (attr) {
              attr.Set(light->area_size, timecode);
            }
            attr = rect_light.GetPrim().CreateAttribute(
                usdtokens::height, pxr::SdfValueTypeNames->Float, true);
            if (attr) {
              attr.Set(light->area_size, timecode);
            }
          }

          usd_light = rect_light;
          break;
        }
      }
      break;
    case LA_LOCAL: {
      pxr::UsdLuxSphereLight sphere_light =
          (usd_export_context_.export_params.export_as_overs) ?
              pxr::UsdLuxSphereLight(
                  usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path)) :
              pxr::UsdLuxSphereLight::Define(usd_export_context_.stage,
                                             usd_export_context_.usd_path);
      sphere_light.CreateRadiusAttr().Set(light->area_size * radius_scale, timecode);

      if (usd_export_context_.export_params.backward_compatible) {
        if (pxr::UsdAttribute attr = sphere_light.GetPrim().CreateAttribute(
                usdtokens::radius, pxr::SdfValueTypeNames->Float, true)) {
          attr.Set(light->area_size * radius_scale, timecode);
        }
      }

      usd_light = sphere_light;
      break;
    }
    case LA_SPOT: {
      pxr::UsdLuxSphereLight spot_light =
          (usd_export_context_.export_params.export_as_overs) ?
              pxr::UsdLuxSphereLight(
                  usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path)) :
              pxr::UsdLuxSphereLight::Define(usd_export_context_.stage,
                                             usd_export_context_.usd_path);
      spot_light.CreateRadiusAttr().Set(light->area_size * radius_scale, timecode);

      if (usd_export_context_.export_params.backward_compatible) {
        if (pxr::UsdAttribute attr = spot_light.GetPrim().CreateAttribute(
                usdtokens::radius, pxr::SdfValueTypeNames->Float, true)) {
          attr.Set(light->area_size * radius_scale, timecode);
        }
      }

      pxr::UsdLuxShapingAPI shapingAPI(spot_light);
      float angle = (light->spotsize * (180.0f / (float)M_PI)) /
                    2.0f;  // Blender angle seems to be half of what USD expectes it to be.
      shapingAPI.CreateShapingConeAngleAttr(pxr::VtValue(angle), true);
      shapingAPI.CreateShapingConeSoftnessAttr(pxr::VtValue(light->spotblend), true);
      spot_light.CreateTreatAsPointAttr(pxr::VtValue(true), true);

      usd_light = spot_light;
      break;
    }
    case LA_SUN: {
      pxr::UsdLuxDistantLight sun_light =
          (usd_export_context_.export_params.export_as_overs) ?
              pxr::UsdLuxDistantLight(
                  usd_export_context_.stage->OverridePrim(usd_export_context_.usd_path)) :
              pxr::UsdLuxDistantLight::Define(usd_export_context_.stage,
                                              usd_export_context_.usd_path);
      sun_light.CreateAngleAttr().Set(light->sun_angle, timecode);

      if (usd_export_context_.export_params.backward_compatible) {
        if (pxr::UsdAttribute attr = sun_light.GetPrim().CreateAttribute(
                usdtokens::angle, pxr::SdfValueTypeNames->Float, true)) {
          attr.Set(light->sun_angle, timecode);
        }
      }

      usd_light = sun_light;
      break;
    }
    default:
      BLI_assert(!"is_supported() returned true for unsupported light type");
  }

  float usd_intensity = light->energy * usd_export_context_.export_params.light_intensity_scale;

  if (usd_export_context_.export_params.convert_light_to_nits) {
    usd_intensity /= nits_to_energy_scale_factor(light, world_scale, radius_scale);
  }

  usd_light.CreateIntensityAttr().Set(usd_intensity, timecode);

  usd_light.CreateColorAttr().Set(pxr::GfVec3f(light->r, light->g, light->b), timecode);
  usd_light.CreateSpecularAttr().Set(light->spec_fac, timecode);

  if (usd_export_context_.export_params.backward_compatible) {
    pxr::UsdAttribute attr = usd_light.GetPrim().CreateAttribute(
        usdtokens::intensity, pxr::SdfValueTypeNames->Float, true);
    if (attr) {
      attr.Set(usd_intensity, timecode);
    }
    attr = usd_light.GetPrim().CreateAttribute(
        usdtokens::color, pxr::SdfValueTypeNames->Color3f, true);
    if (attr) {
      attr.Set(pxr::GfVec3f(light->r, light->g, light->b), timecode);
    }
    attr = usd_light.GetPrim().CreateAttribute(
        usdtokens::specular, pxr::SdfValueTypeNames->Float, true);
    if (attr) {
      attr.Set(light->spec_fac, timecode);
    }
  }

  if (usd_export_context_.export_params.export_custom_properties && light) {
    auto prim = usd_light.GetPrim();
    write_id_properties(prim, light->id, timecode);
  }
}

}  // namespace blender::io::usd
