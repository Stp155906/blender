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
 * The Original Code is Copyright (C) 2021, Blender Foundation
 */

/** \file
 * \ingroup edarmature
 */

#include <math.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_dlrbTree.h"
#include "BLI_listbase.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_action.h"
#include "BKE_animsys.h"
#include "BKE_armature.h"
#include "BKE_idprop.h"
#include "BKE_lib_id.h"
#include "BKE_main.h"
#include "BKE_object.h"

#include "BKE_context.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "WM_api.h"
#include "WM_types.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "ED_anim_api.h"
#include "ED_armature.h"
#include "ED_keyframes_draw.h"
#include "ED_keyframes_edit.h"
#include "ED_keyframing.h"
#include "ED_object.h"
#include "ED_screen.h"

#include "armature_intern.h"

typedef enum ePoseBlendState {
  POSE_BLEND_INIT,
  POSE_BLEND_BLENDING,
  POSE_BLEND_ORIGINAL,
  POSE_BLEND_CONFIRM,
  POSE_BLEND_CANCEL,
} ePoseBlendState;

typedef struct PoseBlendData {
  ePoseBlendState state;
  bool needs_redraw;
  bool is_bone_selection_relevant;

  /** PoseChannelBackup structs for restoring poses. */
  ListBase backups;

  Object *ob;   /* Object to work on. */
  bAction *act; /* Pose to blend in. */

  Scene *scene;  /* For auto-keying. */
  ScrArea *area; /* For drawing status text. */
} PoseBlendData;

/* simple struct for storing backup info for one pose channel */
typedef struct PoseChannelBackup {
  struct PoseChannelBackup *next, *prev;

  bPoseChannel *pchan; /* Pose channel this backup is for. */

  bPoseChannel olddata; /* Backup of pose channel. */
  IDProperty *oldprops; /* Backup copy (needs freeing) of pose channel's ID properties. */
} PoseChannelBackup;

/* Makes a copy of the current pose for restoration purposes - doesn't do constraints currently */
static void poselib_backup_posecopy(PoseBlendData *pbd)
{
  /* TODO(Sybren): reuse same approach as in `armature_pose.cc` in this function. */

  /* See if bone selection is relevant. */
  bool all_bones_selected = true;
  bool no_bones_selected = true;
  LISTBASE_FOREACH (bPoseChannel *, pchan, &pbd->ob->pose->chanbase) {
    const bool is_selected = (pchan->bone->flag & BONE_SELECTED) != 0;
    all_bones_selected &= is_selected;
    no_bones_selected &= !is_selected;
  }

  /* If no bones are selected, act as if all are. */
  pbd->is_bone_selection_relevant = !all_bones_selected && !no_bones_selected;

  LISTBASE_FOREACH (bActionGroup *, agrp, &pbd->act->groups) {
    bPoseChannel *pchan = BKE_pose_channel_find_name(pbd->ob->pose, agrp->name);
    if (pchan == NULL) {
      continue;
    }

    if (pbd->is_bone_selection_relevant && (pchan->bone->flag & BONE_SELECTED) == 0) {
      continue;
    }

    PoseChannelBackup *chan_bak;
    chan_bak = MEM_callocN(sizeof(*chan_bak), "PoseChannelBackup");
    chan_bak->pchan = pchan;
    memcpy(&chan_bak->olddata, chan_bak->pchan, sizeof(chan_bak->olddata));

    if (pchan->prop) {
      chan_bak->oldprops = IDP_CopyProperty(pchan->prop);
    }

    BLI_addtail(&pbd->backups, chan_bak);
  }

  if (pbd->state == POSE_BLEND_INIT) {
    /* Ready for blending now. */
    pbd->state = POSE_BLEND_BLENDING;
  }
}

/* Restores backed-up pose. */
static void poselib_backup_restore(PoseBlendData *pbd)
{
  LISTBASE_FOREACH (PoseChannelBackup *, chan_bak, &pbd->backups) {
    memcpy(chan_bak->pchan, &chan_bak->olddata, sizeof(chan_bak->olddata));

    if (chan_bak->oldprops) {
      IDP_SyncGroupValues(chan_bak->pchan->prop, chan_bak->oldprops);
    }

    /* TODO: constraints settings aren't restored yet,
     * even though these could change (though not that likely) */
  }
}

/* Free list of backups, including any side data it may use. */
static void poselib_backup_free_data(PoseBlendData *pbd)
{
  for (PoseChannelBackup *chan_bak = pbd->backups.first; chan_bak;) {
    PoseChannelBackup *next = chan_bak->next;

    if (chan_bak->oldprops) {
      IDP_FreeProperty(chan_bak->oldprops);
    }
    BLI_freelinkN(&pbd->backups, chan_bak);

    chan_bak = next;
  }
}

/* ---------------------------- */

/* Auto-key/tag bones affected by the pose Action. */
static void poselib_keytag_pose(bContext *C, Scene *scene, PoseBlendData *pbd)
{
  bPose *pose = pbd->ob->pose;
  bAction *act = pbd->act;
  bActionGroup *agrp;

  KeyingSet *ks = ANIM_get_keyingset_for_autokeying(scene, ANIM_KS_WHOLE_CHARACTER_ID);
  ListBase dsources = {NULL, NULL};
  const bool autokey = autokeyframe_cfra_can_key(scene, &pbd->ob->id);

  /* start tagging/keying */
  for (agrp = act->groups.first; agrp; agrp = agrp->next) {
    /* only for selected bones unless there aren't any selected, in which case all are included  */
    bPoseChannel *pchan = BKE_pose_channel_find_name(pose, agrp->name);
    if (pchan == NULL) {
      continue;
    }

    if (pbd->is_bone_selection_relevant && (pchan->bone->flag & BONE_SELECTED) == 0) {
      continue;
    }

    if (autokey) {
      /* Add data-source override for the PoseChannel, to be used later. */
      ANIM_relative_keyingset_add_source(&dsources, &pbd->ob->id, &RNA_PoseBone, pchan);

      /* clear any unkeyed tags */
      if (pchan->bone) {
        pchan->bone->flag &= ~BONE_UNKEYED;
      }
    }
    else {
      /* add unkeyed tags */
      if (pchan->bone) {
        pchan->bone->flag |= BONE_UNKEYED;
      }
    }
  }

  /* perform actual auto-keying now */
  if (autokey) {
    /* insert keyframes for all relevant bones in one go */
    ANIM_apply_keyingset(C, &dsources, NULL, ks, MODIFYKEY_MODE_INSERT, (float)CFRA);
    BLI_freelistN(&dsources);
  }

  /* send notifiers for this */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);
}

/* Apply the relevant changes to the pose */
static void poselib_blend_apply(bContext *C, wmOperator *op)
{
  PoseBlendData *pbd = (PoseBlendData *)op->customdata;

  if (pbd->state == POSE_BLEND_BLENDING) {
    /* TODO(Sybren): implement these: */
    ED_workspace_status_text(C,
                             TIP_("Tab: show original pose; Mousewheel: change blend percentage"));
  }
  else {
    ED_workspace_status_text(C, TIP_("Tab: show blended pose"));
  }

  if (!pbd->needs_redraw) {
    return;
  }
  pbd->needs_redraw = false;

  poselib_backup_restore(pbd);

  /* The pose needs updating, whether it's for restoring the original pose or for showing the
   * result of the blend. */
  DEG_id_tag_update(&pbd->ob->id, ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_OBJECT | ND_POSE, pbd->ob);

  if (pbd->state != POSE_BLEND_BLENDING) {
    return;
  }

  /* Perform the actual blending. */
  struct Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(depsgraph, 0.0f);
  /* TODO(Sybren): blend instead of just apply. */
  BKE_pose_apply_action(pbd->ob, pbd->act, &anim_eval_context);
}

/* ---------------------------- */

/* Return operator return value. */
static int poselib_blend_handle_event(bContext *UNUSED(C), wmOperator *op, const wmEvent *event)
{
  PoseBlendData *pbd = op->customdata;

  /* only accept 'press' event, and ignore 'release', so that we don't get double actions */
  if (ELEM(event->val, KM_PRESS, KM_NOTHING) == 0) {
#if 0
    printf("PoseLib: skipping event with type '%s' and val %d\n",
           WM_key_event_string(event->type, false),
           event->val);
#endif
    return OPERATOR_RUNNING_MODAL;
  }

  if (ELEM(event->type,
           EVT_HOMEKEY,
           EVT_PAD0,
           EVT_PAD1,
           EVT_PAD2,
           EVT_PAD3,
           EVT_PAD4,
           EVT_PAD5,
           EVT_PAD6,
           EVT_PAD7,
           EVT_PAD8,
           EVT_PAD9,
           EVT_PADMINUS,
           EVT_PADPLUSKEY,
           MIDDLEMOUSE,
           MOUSEMOVE)) {
    /* Pass-through of view manipulation events. */
    return OPERATOR_PASS_THROUGH;
  }

  /* NORMAL EVENT HANDLING... */
  /* searching takes priority over normal activity */
  switch (event->type) {
    /* Exit - cancel. */
    case EVT_ESCKEY:
    case RIGHTMOUSE:
      pbd->state = POSE_BLEND_CANCEL;
      break;

    /* Exit - confirm. */
    case LEFTMOUSE:
    case EVT_RETKEY:
    case EVT_PADENTER:
    case EVT_SPACEKEY:
      pbd->state = POSE_BLEND_CONFIRM;
      break;

    /* TODO(Sybren): toggle between original pose and poselib pose. */
    case EVT_TABKEY:
      pbd->state = pbd->state == POSE_BLEND_BLENDING ? POSE_BLEND_ORIGINAL : POSE_BLEND_BLENDING;
      pbd->needs_redraw = true;
      break;

    /* TODO(Sybren): add events for changing the blend amount. */
    case WHEELUPMOUSE:
      pbd->needs_redraw = true;
      break;
    case WHEELDOWNMOUSE:
      pbd->needs_redraw = true;
      break;
  }

  return OPERATOR_RUNNING_MODAL;
}

/* ---------------------------- */

/* Get object that Pose Lib should be found on */
/* XXX C can be zero */
static Object *get_poselib_object(bContext *C)
{
  ScrArea *area;

  /* sanity check */
  if (C == NULL) {
    return NULL;
  }

  area = CTX_wm_area(C);

  if (area && (area->spacetype == SPACE_PROPERTIES)) {
    return ED_object_context(C);
  }
  return BKE_object_pose_armature_get(CTX_data_active_object(C));
}

/* Return true on success, false if the context isn't suitable. */
static bool poselib_blend_init_data(bContext *C, wmOperator *op)
{
  op->customdata = NULL;

  /* check if valid poselib */
  Object *ob = get_poselib_object(C);
  if (ELEM(NULL, ob, ob->pose, ob->data)) {
    BKE_report(op->reports, RPT_ERROR, "Pose lib is only for armatures in pose mode");
    return false;
  }

  /* TODO(Sybren): properly get action from context. */
  ID *id = CTX_data_pointer_get_type(C, "id", &RNA_ID).data;
  if (id == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Context does not contain 'id'");
    return false;
  }
  if (GS(id->name) != ID_AC) {
    BKE_reportf(op->reports, RPT_ERROR, "Context key 'id' (%s) is not an Action", id->name);
    return false;
  }

  /* Set up blend state info. */
  PoseBlendData *pbd;
  op->customdata = pbd = MEM_callocN(sizeof(PoseBlendData), "PoseLib Preview Data");

  /* get basic data */
  pbd->ob = ob;
  pbd->ob->pose = ob->pose;
  pbd->act = (bAction *)id;

  pbd->scene = CTX_data_scene(C);
  pbd->area = CTX_wm_area(C);

  pbd->state = POSE_BLEND_INIT;
  pbd->needs_redraw = true;

  /* Make backups for blending and restoring the pose. */
  poselib_backup_posecopy(pbd);

  /* Set pose flags to ensure the depsgraph evaluation doesn't overwrite it. */
  pbd->ob->pose->flag &= ~POSE_DO_UNLOCK;
  pbd->ob->pose->flag |= POSE_LOCKED;

  return true;
}

/* After previewing poses */
static void poselib_blend_cleanup(bContext *C, wmOperator *op)
{
  PoseBlendData *pbd = (PoseBlendData *)op->customdata;

  /* Redraw the header so that it doesn't show any of our stuff anymore. */
  ED_area_status_text(pbd->area, NULL);
  ED_workspace_status_text(C, NULL);

  /* This signals the depsgraph to unlock and reevaluate the pose on the next evaluation. */
  bPose *pose = pbd->ob->pose;
  pose->flag |= POSE_DO_UNLOCK;

  switch (pbd->state) {
    case POSE_BLEND_CONFIRM: {
      Scene *scene = pbd->scene;
      poselib_keytag_pose(C, scene, pbd);
      break;
    }

    case POSE_BLEND_INIT:
    case POSE_BLEND_BLENDING:
    case POSE_BLEND_ORIGINAL:
      /* Cleanup should not be called directly from these states. */
      BLI_assert(!"poselib_blend_cleanup: unexpected pose blend state");
      BKE_report(op->reports, RPT_ERROR, "Internal pose library error, cancelling operator");
      ATTR_FALLTHROUGH;
    case POSE_BLEND_CANCEL:
      poselib_backup_restore(pbd);
      break;
  }

  DEG_id_tag_update(&pbd->ob->id, ID_RECALC_GEOMETRY);
  WM_event_add_notifier(C, NC_OBJECT | ND_POSE, pbd->ob);

  /* Free temp data for operator */
  poselib_backup_free_data(pbd);
  MEM_SAFE_FREE(op->customdata);
}

static int poselib_blend_exit(bContext *C, wmOperator *op)
{
  PoseBlendData *pbd = op->customdata;
  const ePoseBlendState exit_state = pbd->state;

  poselib_blend_cleanup(C, op);

  if (exit_state == POSE_BLEND_CANCEL) {
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

/* Cancel previewing operation (called when exiting Blender) */
static void poselib_blend_cancel(bContext *C, wmOperator *op)
{
  poselib_blend_exit(C, op);
}

/* Main modal status check. */
static int poselib_blend_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  const int operator_result = poselib_blend_handle_event(C, op, event);

  const PoseBlendData *pbd = op->customdata;
  if (ELEM(pbd->state, POSE_BLEND_CONFIRM, POSE_BLEND_CANCEL)) {
    return poselib_blend_exit(C, op);
  }

  if (pbd->needs_redraw) {
    poselib_blend_apply(C, op);
  }

  return operator_result;
}

/* Modal Operator init. */
static int poselib_blend_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  if (!poselib_blend_init_data(C, op)) {
    return OPERATOR_CANCELLED;
  }

  /* Do initial apply to have something to look at. */
  poselib_blend_apply(C, op);

  WM_event_add_modal_handler(C, op);
  return OPERATOR_RUNNING_MODAL;
}

/* Single-shot apply. */
static int poselib_blend_exec(bContext *C, wmOperator *op)
{
  if (!poselib_blend_init_data(C, op)) {
    return OPERATOR_CANCELLED;
  }

  poselib_blend_apply(C, op);

  PoseBlendData *pbd = op->customdata;
  pbd->state = POSE_BLEND_CONFIRM;
  return poselib_blend_exit(C, op);
}

/* Poll callback for operators that require existing PoseLib data (with poses) to work. */
static bool poselib_blend_poll(bContext *C)
{
  Object *ob = get_poselib_object(C);
  if (ELEM(NULL, ob, ob->pose, ob->data)) {
    /* Pose lib is only for armatures in pose mode. */
    return false;
  }
  return true;
}

void POSELIB_OT_blend_pose(wmOperatorType *ot)
{
  /* Identifiers: */
  ot->name = "Blend Pose Library Pose";
  ot->idname = "POSELIB_OT_blend_pose";
  ot->description = "Blend the given Pose Action to the rig";

  /* Callbacks: */
  ot->invoke = poselib_blend_invoke;
  ot->modal = poselib_blend_modal;
  ot->cancel = poselib_blend_cancel;
  ot->exec = poselib_blend_exec;
  ot->poll = poselib_blend_poll;

  /* Flags: */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties: */
  // RNA_def_pointer(ot->srna, "pose_action", "Action", "Pose Action", "Action to apply as pose");

#if 0
  RNA_def_float_factor(ot->srna,
                       "blend_factor",
                       1.0f,
                       0.0f,
                       1.0f,
                       "Blend Factor",
                       "Amount that the pose is applied on top of the existing poses",
                       0.0f,
                       1.0f);
#endif
}
