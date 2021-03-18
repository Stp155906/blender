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
 * The Original Code is Copyright (C) 2020 Blender Foundation
 * All rights reserved.
 */
#pragma once

/** \file
 * \ingroup bgpencil
 */
#include <string>
#include <vector>

#include "BLI_float2.hh"
#include "BLI_vector.hh"

#include "gpencil_io.h"

struct ARegion;
struct Depsgraph;
struct Main;
struct Object;
struct RegionView3D;
struct Scene;

struct bGPdata;
struct bGPDlayer;
struct bGPDframe;
struct bGPDstroke;
struct MaterialGPencilStyle;

using blender::Vector;

namespace blender::io::gpencil {

class GpencilIO {

 public:
  GpencilIO(const struct GpencilIOParams *iparams);

  void frame_number_set(const int value);

 protected:
  GpencilIOParams params_;

  bool invert_axis_[2];
  float diff_mat_[4][4];
  char filename_[FILE_MAX];

  /* Used for sorting objects. */
  struct ObjectZ {
    float zdepth;
    struct Object *ob;
  };

  /** List of included objects. */
  blender::Vector<ObjectZ> ob_list_;

  /* Data for easy access. */
  struct Depsgraph *depsgraph_;
  struct bGPdata *gpd_;
  struct Main *bmain_;
  struct Scene *scene_;
  struct RegionView3D *rv3d_;

  int16_t winx_, winy_;
  int16_t render_x_, render_y_;
  float camera_ratio_;
  rctf camera_rect_;

  float2 offset_;

  int cfra_;
  bool object_created_;

  float stroke_color_[4], fill_color_[4];

  static std::string rgb_to_hexstr(float color[3]);
  static float stroke_average_pressure_get(struct bGPDstroke *gps);
  static bool is_stroke_thickness_constant(struct bGPDstroke *gps);

  /* Geometry functions. */
  bool gpencil_3d_point_to_screen_space(const float co[3], float r_co[2]);
  void gpencil_3d_point_to_render_space(const float co[3], float r_co[2]);
  void gpencil_3d_point_to_2D(const float co[3], float r_co[2]);

  float stroke_point_radius_get(struct bGPDlayer *gpl, struct bGPDstroke *gps);
  void create_object_list();

  struct MaterialGPencilStyle *gp_style_current_get();
  bool material_is_stroke();
  bool material_is_fill();

  bool is_camera_mode();

  float stroke_average_opacity_get();

  void gpl_matrix_set(struct Object *ob, struct bGPDlayer *gpl);
  void gps_current_color_set(struct Object *ob, struct bGPDstroke *gps);

  void selected_objects_boundbox_set();
  void selected_objects_boundbox_get(rctf *boundbox);
  void filename_set(const char *filename);

 private:
  struct MaterialGPencilStyle *gp_style_;
  bool is_stroke_;
  bool is_fill_;
  float avg_opacity_;
  bool is_camera_;
  rctf select_boundbox_;

  /* Camera matrix. */
  float persmat_[4][4];
};

}  // namespace blender::io::gpencil
