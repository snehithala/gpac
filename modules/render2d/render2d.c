/*
 *			GPAC - Multimedia Framework C SDK
 *
 *			Copyright (c) Jean Le Feuvre 2000-2005
 *					All rights reserved
 *
 *  This file is part of GPAC / 2D rendering module
 *
 *  GPAC is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  GPAC is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *   
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */


#include "render2d.h"
#include "stacks2d.h"
#include "visualsurface2d.h"
#include <gpac/options.h>

#ifndef GPAC_DISABLE_SVG
#include "svg/svg_stacks.h"
#endif

void R2D_MapCoordsToAR(GF_VisualRenderer *vr, s32 inX, s32 inY, Fixed *x, Fixed *y)
{
	Render2D *sr = (Render2D*)vr->user_priv;


	/*revert to BIFS like*/
	inX = inX - sr->compositor->width /2;
	inY = sr->compositor->height/2 - inY;
	*x = INT2FIX(inX);
	*y = INT2FIX(inY);

	/*if no size info scaling is never applied*/
	if (!sr->compositor->has_size_info) return;

	if (sr->scalable_zoom) {
		*x = gf_muldiv(*x, INT2FIX(sr->cur_width), INT2FIX(sr->out_width));
		*y = gf_muldiv(*y, INT2FIX(sr->cur_height), INT2FIX(sr->out_height));
	} else {
		*x -= INT2FIX( ((s32)sr->out_width - (s32)sr->compositor->scene_width) / 2 );
		*y += INT2FIX( ((s32)sr->out_height - (s32)sr->compositor->scene_height) / 2 );
		*x = gf_muldiv(*x, INT2FIX(sr->compositor->scene_width ), INT2FIX(sr->out_width));
		*y = gf_muldiv(*y, INT2FIX(sr->compositor->scene_height), INT2FIX(sr->out_height));
	}
}

static void R2D_SetZoom(Render2D *sr, Fixed zoom) 
{
	Fixed ratio;

	gf_sr_lock(sr->compositor, 1);
	if (zoom <= 0) zoom = FIX_ONE/1000;
	if (zoom != sr->zoom) {
		ratio = gf_divfix(zoom, sr->zoom);
		sr->trans_x = gf_mulfix(sr->trans_x, ratio);
		sr->trans_y = gf_mulfix(sr->trans_y, ratio);
		sr->zoom = zoom;
	}
	gf_mx2d_init(sr->top_effect->transform);
	gf_mx2d_add_scale(&sr->top_effect->transform, sr->scale_x, sr->scale_y);
	gf_mx2d_add_scale(&sr->top_effect->transform, sr->zoom, sr->zoom);
	gf_mx2d_add_translation(&sr->top_effect->transform, sr->trans_x, sr->trans_y);
	sr->compositor->draw_next_frame = 1;
	sr->top_effect->invalidate_all = 1;
	gf_sr_lock(sr->compositor, 0);
}

void R2D_SetScaling(Render2D *sr, Fixed scaleX, Fixed scaleY)
{
	sr->scale_x = scaleX;
	sr->scale_y = scaleY;
	R2D_SetZoom(sr, sr->zoom);
}

void R2D_ResetSurfaces(Render2D *sr)
{
	u32 i;
	for (i=0; i<gf_list_count(sr->surfaces_2D); i++) {
		VisualSurface2D *surf = gf_list_get(sr->surfaces_2D, i);
		while (gf_list_count(surf->prev_nodes_drawn)) gf_list_rem(surf->prev_nodes_drawn, 0);
		surf->to_redraw.count = 0;
		VS2D_ResetSensors(surf);
	}
}

void R2D_SceneReset(GF_VisualRenderer *vr)
{
	u32 flag;
	Render2D *sr = (Render2D*) vr->user_priv;
	if (!sr) return;
	R2D_ResetSurfaces(sr);
	while (gf_list_count(sr->sensors)) {
		gf_list_rem(sr->sensors, 0);
	}

	flag = sr->top_effect->trav_flags;
	effect_reset(sr->top_effect);
	sr->top_effect->trav_flags = flag;
	sr->compositor->reset_graphics = 1;
	sr->trans_x = sr->trans_y = 0;
	sr->zoom = FIX_ONE;
	R2D_SetScaling(sr, sr->scale_x, sr->scale_y);
	/*force resetup of main surface in case we're switching coord system*/
	sr->main_surface_setup = 0;
	VS2D_ResetGraphics(sr->surface);
}

GF_Rect R2D_ClipperToPixelMetrics(RenderEffect2D *eff, SFVec2f size)
{
	GF_Rect res;

	if (eff->surface->composite) {
		res.width = INT2FIX(eff->surface->width);
		res.height = INT2FIX(eff->surface->height);
	} else {
		res.width = INT2FIX(eff->surface->render->compositor->scene_width);
		res.height = INT2FIX(eff->surface->render->compositor->scene_height);
	}
	if (eff->is_pixel_metrics) {
		if (size.x>=0) res.width = size.x;
		if (size.y>=0) res.height = size.y;
	} else {
		if (size.x>=0) res.width = gf_mulfix(res.width, size.x / 2);
		if (size.y>=0) res.height = gf_mulfix(res.height, size.y / 2);
	}
	res = gf_rect_center(res.width, res.height);
	return res;
}


void R2D_RegisterSurface(Render2D *sr, struct _visual_surface_2D  *surf)
{
	if (R2D_IsSurfaceRegistered(sr, surf)) return;
	gf_list_add(sr->surfaces_2D, surf);
}

void R2D_UnregisterSurface(Render2D *sr, struct _visual_surface_2D  *surf)
{
	gf_list_del_item(sr->surfaces_2D, surf);
}

Bool R2D_IsSurfaceRegistered(Render2D *sr, struct _visual_surface_2D *surf)
{
	u32 i;
	for (i=0; i<gf_list_count(sr->surfaces_2D); i++) {
		if (gf_list_get(sr->surfaces_2D, i) == surf) return 1;
	}
	return 0;
}

void effect_add_sensor(RenderEffect2D *eff, SensorHandler *ptr, GF_Matrix2D *mat)
{
	SensorContext *ctx;
	if (!ptr) return;
	ctx = malloc(sizeof(SensorContext));
	ctx->h_node = ptr;
	
	if (mat) {
		gf_mx2d_copy(ctx->matrix, *mat);
	} else {
		gf_mx2d_init(ctx->matrix);
	}
	gf_list_add(eff->sensors, ctx);
}

void effect_reset_sensors(RenderEffect2D *eff)
{
	SensorContext *ctx;
	while (gf_list_count(eff->sensors)) {
		ctx = gf_list_get(eff->sensors, 0);
		gf_list_rem(eff->sensors, 0);
		free(ctx);
	}
}

void effect_reset(RenderEffect2D *eff)
{
	GF_List *bck = eff->sensors;
	memset(eff, 0, sizeof(RenderEffect2D));
	eff->sensors = bck;
	if (bck) effect_reset_sensors(eff);
	gf_mx2d_init(eff->transform);
	gf_cmx_init(&eff->color_mat);
}

void effect_delete(RenderEffect2D *eff)
{
	if (eff->sensors) {
		effect_reset_sensors(eff);
		gf_list_del(eff->sensors);
	}
	free(eff);
}

Bool is_sensor_node(GF_Node *node)
{
	switch (gf_node_get_tag(node)) {
	case TAG_MPEG4_TouchSensor:
	case TAG_MPEG4_PlaneSensor2D:
	case TAG_MPEG4_DiscSensor:
	case TAG_MPEG4_ProximitySensor2D: 
		return 1;

		/*anchor is not considered as a child sensor node when picking sensors*/
	/*case TAG_MPEG4_Anchor:*/
#ifndef GPAC_DISABLE_SVG
	/*case TAG_SVG_a: */
#endif
	default:
		return 0;
	}
}

SensorHandler *get_sensor_handler(GF_Node *n)
{
	SensorHandler *hs;

	switch (gf_node_get_tag(n)) {
	case TAG_MPEG4_Anchor: hs = r2d_anchor_get_handler(n); break;
	case TAG_MPEG4_DiscSensor: hs = r2d_ds_get_handler(n); break;
	case TAG_MPEG4_TouchSensor: hs = r2d_touch_sensor_get_handler(n); break;
	case TAG_MPEG4_PlaneSensor2D: hs = r2d_ps2D_get_handler(n); break;
	case TAG_MPEG4_ProximitySensor2D: hs = r2d_prox2D_get_handler(n); break;
#ifndef GPAC_DISABLE_SVG
	case TAG_SVG_a: hs = SVG_GetHandler_a(n); break;
#endif
	default:
		return NULL;
	}
	if (hs && hs->IsEnabled(hs)) return hs;
	return NULL;
}

void R2D_RegisterSensor(GF_Renderer *compositor, SensorHandler *sh)
{
	u32 i;
	Render2D *sr = (Render2D *)compositor->visual_renderer->user_priv;
	for (i=0; i<gf_list_count(sr->sensors); i++) {
		if (gf_list_get(sr->sensors, i) == sh) return;
	}
	gf_list_add(sr->sensors, sh);
}

void R2D_UnregisterSensor(GF_Renderer *compositor, SensorHandler *sh)
{
	Render2D *sr = (Render2D *)compositor->visual_renderer->user_priv;
	gf_list_del_item(sr->sensors, sh);
}


#define R2DSETCURSOR(t) { GF_Event evt; evt.type = GF_EVT_SET_CURSOR; evt.cursor.cursor_type = (t); sr->compositor->video_out->ProcessEvent(sr->compositor->video_out, &evt); }

Bool R2D_ExecuteEvent(GF_VisualRenderer *vr, GF_UserEvent *event)
{
	u32 i, type, count;
	Bool act;
	s32 key_inv;
	Fixed key_trans;
	DrawableContext *ctx;
	UserEvent2D evt, *ev;
	Render2D *sr = (Render2D *)vr->user_priv;

	evt.context = NULL;
	evt.event_type = event->event_type;
	evt.x = 0;
	evt.y = 0;
	ev = &evt;

	if (event->event_type<=GF_EVT_MOUSEWHEEL) R2D_MapCoordsToAR(vr, event->mouse.x, event->mouse.y, &evt.x, &evt.y);
	if (event->event_type>GF_EVT_LEFTUP) goto no_sensor;
	
	if (sr->is_tracking) {
		/*in case a node is inserted at the depth level of a node previously tracked (rrrhhhaaaa...) */
		if (sr->grab_ctx && sr->grab_ctx->node != sr->grab_node) {
			sr->is_tracking = 0;
			sr->grab_ctx = NULL;
		}
	}
	
	if (!sr->is_tracking) {
		ctx = VS2D_FindNode(sr->surface, ev->x, ev->y);
		sr->grab_ctx = ctx;
		if (ctx) sr->grab_node = ctx->node;
	} else {
		ctx = sr->grab_ctx;
	}

	//3- mark all sensors of the context to skip deactivation
	ev->context = ctx;
	if (ctx) {	
		SensorContext *sc;
		count = gf_list_count(ctx->sensors);
		for (i=0; i<count; i++) {
			SensorContext *sc = gf_list_get(ctx->sensors, i);
			sc->h_node->skip_second_pass = 1;
		}

		sc = gf_list_get(ctx->sensors, count-1);
		//also notify the app we're above a sensor
		type = GF_CURSOR_NORMAL;
		switch (gf_node_get_tag(sc->h_node->owner)) {
		case TAG_MPEG4_Anchor: type = GF_CURSOR_ANCHOR; break;
		case TAG_MPEG4_PlaneSensor2D: type = GF_CURSOR_PLANE; break;
		case TAG_MPEG4_DiscSensor: type = GF_CURSOR_ROTATE; break;
		case TAG_MPEG4_ProximitySensor2D: type = GF_CURSOR_PROXIMITY; break;
		case TAG_MPEG4_TouchSensor: type = GF_CURSOR_TOUCH; break;
#ifndef GPAC_DISABLE_SVG
		case TAG_SVG_a: type = GF_CURSOR_ANCHOR; break;
#endif
		}
		if (type != GF_CURSOR_NORMAL) {
			if (sr->last_sensor != type) {
				GF_Event evt;
				evt.type = GF_EVT_SET_CURSOR;
				evt.cursor.cursor_type = type;
				sr->compositor->video_out->ProcessEvent(sr->compositor->video_out, &evt);
				sr->last_sensor = type;
			}
		}
	}

	if (!ctx && (sr->last_sensor != GF_CURSOR_NORMAL)) {
		R2DSETCURSOR(GF_CURSOR_NORMAL);
		sr->last_sensor = GF_CURSOR_NORMAL;
	}

	/*deactivate all other registered sensors*/
	ev->context = NULL;
	for (i=0; i< gf_list_count(sr->sensors); i++) {
		SensorHandler *sh = gf_list_get(sr->sensors, i);
		act = ! sh->skip_second_pass;
		sh->skip_second_pass = 0;
		count = gf_list_count(sr->sensors);
		if (act)
			sh->OnUserEvent(sh, ev, NULL);
		if (count != gf_list_count(sr->sensors)) i-= 1;
	}	
	
	/*activate current one if any*/
	if (ctx) {
		ev->context = ctx;
		for (i=gf_list_count(ctx->sensors); i>0; i--) {
			SensorContext *sc = gf_list_get(ctx->sensors, i-1);
			sc->h_node->skip_second_pass = 0;
			sc->h_node->OnUserEvent(sc->h_node, ev, &sc->matrix);
		}
		return 1;
	}


no_sensor:
	/*no object, perform zoom & pan*/
	if (!(sr->compositor->interaction_level & GF_INTERACT_NAVIGATION) || !sr->navigate_mode) return 0;

	key_inv = 1;
	key_trans = 2*FIX_ONE;
	if (sr->compositor->key_states&GF_KM_SHIFT) key_trans *= 4;

	switch (event->event_type) {
	case GF_EVT_LEFTDOWN:
		sr->grab_x = ev->x;
		sr->grab_y = ev->y;
		sr->grabbed = 1;
		break;
		break;
	case GF_EVT_LEFTUP:
		sr->grabbed = 0;
		break;
	case GF_EVT_MOUSEMOVE:
		if (sr->grabbed && (sr->navigate_mode == GF_NAVIGATE_SLIDE)) {
			Fixed dx, dy;
			dx = ev->x - sr->grab_x;
			dy = ev->y - sr->grab_y;
			if (! gf_sg_use_pixel_metrics(sr->compositor->scene)) {
				dx /= sr->cur_width;
				dy /= sr->cur_height;
			}
			/*set zoom*/
			if (sr->compositor->key_states & GF_KM_CTRL) {
				Fixed new_zoom = sr->zoom;
				if (new_zoom > FIX_ONE) new_zoom += dy/10;
				else new_zoom += dy/40;
				R2D_SetZoom(sr, new_zoom);
			}
			/*set pan*/
			else {
				sr->trans_x += dx;
				sr->trans_y += dy;
				R2D_SetZoom(sr, sr->zoom);
			}
			sr->grab_x = ev->x;
			sr->grab_y = ev->y;
		}
		break;
	case GF_EVT_VKEYDOWN:
		switch (event->key.vk_code) {
		case GF_VK_HOME:
			if (!sr->grabbed) {
				sr->zoom = FIX_ONE;
				sr->trans_x = sr->trans_y = 0;
				R2D_SetZoom(sr, sr->zoom);
			}
			break;
		case GF_VK_LEFT: key_inv = -1;
		case GF_VK_RIGHT:
			sr->trans_x += key_inv*key_trans;
			R2D_SetZoom(sr, sr->zoom);
			break;
		case GF_VK_DOWN: key_inv = -1;
		case GF_VK_UP:
			if (sr->compositor->key_states & GF_KM_CTRL) {
				Fixed new_zoom = sr->zoom;
				if (new_zoom > FIX_ONE) new_zoom += key_inv*FIX_ONE/10;
				else new_zoom += key_inv*FIX_ONE/20;
				R2D_SetZoom(sr, new_zoom);
			} else {
				sr->trans_y += key_inv*key_trans;
				R2D_SetZoom(sr, sr->zoom);
			}
			break;
		}
		break;
	}
	return 0;
}

void R2D_DrawScene(GF_VisualRenderer *vr)
{
	GF_Window rc;
	u32 i;
	RenderEffect2D static_eff;
	Render2D *sr = (Render2D *)vr->user_priv;
	GF_Node *top_node = gf_sg_get_root_node(sr->compositor->scene);

	if (!sr->compositor->scene || !top_node) return;

	memcpy(&static_eff, sr->top_effect, sizeof(RenderEffect2D));

	if (!sr->main_surface_setup) {
		sr->main_surface_setup = 1;
		sr->surface->center_coords = 1;
		sr->surface->default_back_color = 0xFF000000;

#ifdef GPAC_USE_LASeR
		{
			u32 node_tag = gf_node_get_tag(top_node);
			if ((node_tag>=GF_NODE_RANGE_FIRST_LASER) && (node_tag<=GF_NODE_RANGE_LAST_LASER))
				sr->surface->default_back_color = 0xFFFFFFFF;
		}
#endif
#ifndef GPAC_DISABLE_SVG
		{
			u32 node_tag = gf_node_get_tag(top_node);
			if ((node_tag>=GF_NODE_RANGE_FIRST_SVG) && (node_tag<=GF_NODE_RANGE_LAST_SVG)) {
				sr->surface->default_back_color = 0xFFFFFFFF;
				sr->surface->center_coords = 0;
				sr->main_surface_setup = 2;
			}
		}
#endif
	}

	sr->surface->width = sr->cur_width;
	sr->surface->height = sr->cur_height;

	sr->top_effect->is_pixel_metrics = gf_sg_use_pixel_metrics(sr->compositor->scene);
	sr->top_effect->min_hsize = INT2FIX(MIN(sr->compositor->scene_width, sr->compositor->scene_height)) / 2;

	VS2D_InitDraw(sr->surface, sr->top_effect);
	gf_node_render(top_node, sr->top_effect);

	for (i=0; i<gf_list_count(sr->compositor->extra_scenes); i++) {
		GF_SceneGraph *sg = gf_list_get(sr->compositor->extra_scenes, i);
		GF_Node *n = gf_sg_get_root_node(sg);
		if (n) gf_node_render(n, sr->top_effect);
	}

	VS2D_TerminateDraw(sr->surface, sr->top_effect);
	memcpy(sr->top_effect, &static_eff, sizeof(RenderEffect2D));
	sr->top_effect->invalidate_all = 0;

	/*and flush*/
	rc.x = sr->out_x; 
	rc.y = sr->out_y; 
	rc.w = sr->out_width;	
	rc.h = sr->out_height;		
	sr->compositor->video_out->Flush(sr->compositor->video_out, &rc);
	sr->frame_num++;
}

Bool R2D_IsPixelMetrics(GF_Node *n)
{
	GF_SceneGraph *sg = gf_node_get_graph(n);
	return gf_sg_use_pixel_metrics(sg);
}


static GF_Err R2D_RecomputeAR(GF_VisualRenderer *vr)
{
	Double ratio;
	GF_Event evt;
	Fixed scaleX, scaleY;
	Render2D *sr = (Render2D *)vr->user_priv;
	if (!sr->compositor->scene_height || !sr->compositor->scene_width) return GF_OK;
	if (!sr->compositor->height || !sr->compositor->width) return GF_OK;

	sr->out_width = sr->compositor->width;
	sr->out_height = sr->compositor->height;
	sr->cur_width = sr->compositor->scene_width;
	sr->cur_height = sr->compositor->scene_height;
	sr->out_x = 0;
	sr->out_y = 0;

	/*force complete clean*/
	sr->top_effect->invalidate_all = 1;

	if (!sr->compositor->has_size_info && !(sr->compositor->override_size_flags & 2) ) {
		sr->compositor->scene_width = sr->cur_width = sr->out_width;
		sr->compositor->scene_height = sr->cur_height = sr->out_height;
		R2D_SetScaling(sr, 1, 1);
		/*and resize hardware surface*/
		evt.type = GF_EVT_VIDEO_SETUP;
		evt.size.width = sr->cur_width;
		evt.size.height = sr->cur_height;
		return sr->compositor->video_out->ProcessEvent(sr->compositor->video_out, &evt);
	}

	switch (sr->compositor->aspect_ratio) {
	case GF_ASPECT_RATIO_FILL_SCREEN:
		break;
	case GF_ASPECT_RATIO_16_9:
		sr->out_width = sr->compositor->width;
		sr->out_height = 9 * sr->compositor->width / 16;
		if (sr->out_height>sr->compositor->height) {
			sr->out_height = sr->compositor->height;
			sr->out_width = 16 * sr->compositor->height / 9;
		}
		break;
	case GF_ASPECT_RATIO_4_3:
		sr->out_width = sr->compositor->width;
		sr->out_height = 3 * sr->compositor->width / 4;
		if (sr->out_height>sr->compositor->height) {
			sr->out_height = sr->compositor->height;
			sr->out_width = 4 * sr->compositor->height / 3;
		}
		break;
	default:
		ratio = sr->compositor->scene_height;
		ratio /= sr->compositor->scene_width;
		if (sr->out_width * ratio > sr->out_height) {
			sr->out_width = sr->out_height * sr->compositor->scene_width;
			sr->out_width /= sr->compositor->scene_height;
		}
		else {
			sr->out_height = sr->out_width * sr->compositor->scene_height;
			sr->out_height /= sr->compositor->scene_width;
		}
		break;
	}
	sr->out_x = (sr->compositor->width - sr->out_width) / 2;
	sr->out_y = (sr->compositor->height - sr->out_height) / 2;

	if (!sr->scalable_zoom) {
		sr->cur_width = sr->compositor->scene_width;
		sr->cur_height = sr->compositor->scene_height;
		scaleX = FIX_ONE;
		scaleY = FIX_ONE;
	} else {
		sr->cur_width = sr->out_width;
		sr->cur_height = sr->out_height;
		scaleX = gf_divfix(INT2FIX(sr->out_width), INT2FIX(sr->compositor->scene_width));
		scaleY = gf_divfix(INT2FIX(sr->out_height), INT2FIX(sr->compositor->scene_height));
	}
	/*set scale factor*/
	R2D_SetScaling(sr, scaleX, scaleY);
	gf_sr_invalidate(sr->compositor, NULL);
	/*and resize hardware surface*/
	evt.type = GF_EVT_VIDEO_SETUP;
	evt.size.width = sr->cur_width;
	evt.size.height = sr->cur_height;
	return sr->compositor->video_out->ProcessEvent(sr->compositor->video_out, &evt);
}

GF_Node *R2D_PickNode(GF_VisualRenderer *vr, s32 X, s32 Y)
{
	Fixed x, y;
	GF_Node *res = NULL;
	Render2D *sr = (Render2D *)vr->user_priv;

	if (!sr) return NULL;
	/*lock to prevent any change while picking*/
	gf_sr_lock(sr->compositor, 1);
	if (sr->compositor->scene) {
		R2D_MapCoordsToAR(vr, X, Y, &x, &y);
		res = VS2D_PickNode(sr->surface, x, y);
	}
	gf_sr_lock(sr->compositor, 0);
	return res;
}


GF_Err R2D_GetSurfaceAccess(VisualSurface2D *surf)
{
	GF_Err e;
	Render2D *sr = surf->render;

	if (!surf->the_surface) return GF_BAD_PARAM;
	sr->locked = 0;
	e = GF_IO_ERR;
	
	/*try from device*/
	if (sr->compositor->r2d->surface_attach_to_device && sr->compositor->video_out->LockOSContext) {
		sr->hardware_context = sr->compositor->video_out->LockOSContext(sr->compositor->video_out, 1);
		if (sr->hardware_context) {
			e = sr->compositor->r2d->surface_attach_to_device(surf->the_surface, sr->hardware_context, sr->cur_width, sr->cur_height);
			if (!e) {
				surf->is_attached = 1;
				return GF_OK;
			}
			sr->compositor->video_out->LockOSContext(sr->compositor->video_out, 0);
		}
	}
	
	/*TODO - collect hw accelerated blit routines if any*/

	if (sr->compositor->video_out->LockBackBuffer(sr->compositor->video_out, &sr->hw_surface, 1)==GF_OK) {
		sr->locked = 1;
		e = sr->compositor->r2d->surface_attach_to_buffer(surf->the_surface, sr->hw_surface.video_buffer, 
							sr->hw_surface.width, 
							sr->hw_surface.height,
							sr->hw_surface.pitch,
							sr->hw_surface.pixel_format);
		if (!e) {
			surf->is_attached = 1;
			return GF_OK;
		}
		sr->compositor->video_out->LockBackBuffer(sr->compositor->video_out, NULL, 0);
	}
	sr->locked = 0;
	surf->is_attached = 0;
	return e;		
}

void R2D_ReleaseSurfaceAccess(VisualSurface2D *surf)
{
	Render2D *sr = surf->render;
	if (surf->is_attached) {
		sr->compositor->r2d->surface_detach(surf->the_surface);
		surf->is_attached = 0;
	}
	if (sr->hardware_context) {
		sr->compositor->video_out->LockOSContext(sr->compositor->video_out, 0);
		sr->hardware_context = NULL;
	} else if (sr->locked) {
		sr->compositor->video_out->LockBackBuffer(sr->compositor->video_out, NULL, 0);
		sr->locked = 0;
	}
}

Bool R2D_SupportsFormat(VisualSurface2D *surf, u32 pixel_format)
{
	switch (pixel_format) {
	case GF_PIXEL_RGB_24:
	case GF_PIXEL_BGR_24:
	case GF_PIXEL_RGB_555:
	case GF_PIXEL_RGB_565:
	case GF_PIXEL_ARGB:
	case GF_PIXEL_RGBA:
	case GF_PIXEL_YV12:
	case GF_PIXEL_IYUV:
	case GF_PIXEL_I420:
		return 1;
	/*the rest has to be displayed through brush for now, we only use YUV and RGB pool*/
	default:
		return 0;
	}
}

void R2D_DrawBitmap(VisualSurface2D *surf, struct _gf_sr_texture_handler *txh, GF_IRect *clip, GF_Rect *unclip, u8 alpha, u32 *col_key, GF_ColorMatrix *cmat)
{
	GF_VideoSurface video_src;
	Fixed w_scale, h_scale, tmp;
	GF_Err e;
	Bool use_soft_stretch;
	GF_Window src_wnd, dst_wnd;
	u32 start_x, start_y, cur_width, cur_height;
	GF_IRect clipped_final = *clip;
	GF_Rect final = *unclip;

	if (!txh->data) return;

	if (!surf->render->compositor->has_size_info && !(surf->render->compositor->msg_type & GF_SR_CFG_OVERRIDE_SIZE) 
		&& (surf->render->compositor->override_size_flags & 1) 
		&& !(surf->render->compositor->override_size_flags & 2) 
		) {
		if ( (surf->render->compositor->scene_width < txh->width) 
			|| (surf->render->compositor->scene_height < txh->height)) {
			surf->render->compositor->scene_width = txh->width;
			surf->render->compositor->scene_height = txh->height;
			surf->render->compositor->msg_type |= GF_SR_CFG_OVERRIDE_SIZE;
			return;
		}
	}
	
	/*this should never happen but we check for float rounding safety*/
	if (final.width<=0 || final.height <=0) return;

	w_scale = final.width / txh->width;
	h_scale = final.height / txh->height;

	/*take care of pixel rounding for odd width/height and make sure we strictly draw in the clipped bounds*/
	cur_width = surf->render->cur_width;
	cur_height = surf->render->cur_height;

	if (surf->center_coords) {
		if (cur_width % 2) {
			clipped_final.x += (cur_width-1) / 2;
			final.x += INT2FIX( (cur_width-1) / 2 );
		} else {
			clipped_final.x += cur_width / 2;
			final.x += INT2FIX( cur_width / 2 );
		}
		if (cur_height % 2) {
			clipped_final.y = (cur_height-1) / 2 - clipped_final.y;
			final.y = INT2FIX( (cur_height - 1) / 2) - final.y;
		} else {
			clipped_final.y = cur_height/ 2 - clipped_final.y;
			final.y = INT2FIX( cur_height / 2) - final.y;
		}
	} else {
		final.y -= final.height;
		clipped_final.y -= clipped_final.height;
	}

	/*make sure we lie in the final rect (this is needed for directRender mode)*/
	if (clipped_final.x<0) {
		clipped_final.width += clipped_final.x;
		clipped_final.x = 0;
		if (clipped_final.width <= 0) return;
	}
	if (clipped_final.y<0) {
		clipped_final.height += clipped_final.y;
		clipped_final.y = 0;
		if (clipped_final.height <= 0) return;
	}
	if (clipped_final.x + clipped_final.width > (s32) cur_width) {
		clipped_final.width = cur_width - clipped_final.x;
		clipped_final.x = cur_width - clipped_final.width;
	}
	if (clipped_final.y + clipped_final.height > (s32) cur_height) {
		clipped_final.height = cur_height - clipped_final.y;
		clipped_final.y = cur_height - clipped_final.height;
	}
	/*needed in direct rendering since clipping is not performed*/
	if (clipped_final.width<=0 || clipped_final.height <=0) 
		return;

	/*compute X offset in src bitmap*/
	start_x = 0;
	tmp = INT2FIX(clipped_final.x);
	if (tmp >= final.x)
		start_x = FIX2INT( gf_divfix(tmp - final.x, w_scale) );


	/*compute Y offset in src bitmap*/
	start_y = 0;
	tmp = INT2FIX(clipped_final.y);
	if (tmp >= final.y)
		start_y = FIX2INT( gf_divfix(tmp - final.y, h_scale) );
	
	dst_wnd.x = (u32) clipped_final.x;
	dst_wnd.y = (u32) clipped_final.y;
	dst_wnd.w = (u32) clipped_final.width;
	dst_wnd.h = (u32) clipped_final.height;

	src_wnd.w = FIX2INT( gf_divfix(INT2FIX(dst_wnd.w), w_scale) );
	src_wnd.h = FIX2INT( gf_divfix(INT2FIX(dst_wnd.h), h_scale) );
	if (src_wnd.w>txh->width) src_wnd.w=txh->width;
	if (src_wnd.h>txh->height) src_wnd.h=txh->height;
	
	src_wnd.x = start_x;
	src_wnd.y = start_y;


	if (!src_wnd.w || !src_wnd.h) return;
	/*make sure we lie in src bounds*/
	if (src_wnd.x + src_wnd.w>txh->width) src_wnd.w = txh->width - src_wnd.x;
	if (src_wnd.y + src_wnd.h>txh->height) src_wnd.h = txh->height - src_wnd.y;

	use_soft_stretch = 1;
	if (!cmat && (alpha==0xFF) && surf->render->compositor->video_out->Blit) {
		u32 hw_caps = surf->render->compositor->video_out->hw_caps;
		/*get the right surface and copy the part of the image on it*/
		switch (txh->pixelformat) {
		case GF_PIXEL_RGB_24:
		case GF_PIXEL_BGR_24:
			use_soft_stretch = 0;
			break;
		case GF_PIXEL_YV12:
		case GF_PIXEL_IYUV:
		case GF_PIXEL_I420:
			if (surf->render->enable_yuv_hw && (hw_caps & GF_VIDEO_HW_HAS_YUV))
				use_soft_stretch = 0;
			break;
		default:
			break;
		}
		if (col_key && (GF_COL_A(*col_key) || !(hw_caps & GF_VIDEO_HW_HAS_COLOR_KEY))) use_soft_stretch = 1;
	}

	/*most graphic cards can't perform bliting on locked surface - force unlock by releasing the hardware*/
	VS2D_TerminateSurface(surf);

	video_src.height = txh->height;
	video_src.width = txh->width;
	video_src.pitch = txh->stride;
	video_src.pixel_format = txh->pixelformat;
	video_src.video_buffer = txh->data;
	if (!use_soft_stretch) {
		e = surf->render->compositor->video_out->Blit(surf->render->compositor->video_out, &video_src, &src_wnd, &dst_wnd, col_key);
		/*HW pb, try soft*/
		if (e) {
			fprintf(stdout, "Error during hardware blit - trying with soft one\n");
			use_soft_stretch = 1;
		}
	}
	if (use_soft_stretch) {
		GF_VideoSurface backbuffer;
		e = surf->render->compositor->video_out->LockBackBuffer(surf->render->compositor->video_out, &backbuffer, 1);
		gf_stretch_bits(&backbuffer, &video_src, &dst_wnd, &src_wnd, 0, alpha, 0, col_key, cmat);
		e = surf->render->compositor->video_out->LockBackBuffer(surf->render->compositor->video_out, &backbuffer, 0);
	}
	VS2D_InitSurface(surf);
}

GF_Err R2D_LoadRenderer(GF_VisualRenderer *vr, GF_Renderer *compositor)
{
	Render2D *sr;
	const char *sOpt;
	if (vr->user_priv) return GF_BAD_PARAM;

	sr = malloc(sizeof(Render2D));
	if (!sr) return GF_OUT_OF_MEM;
	memset(sr, 0, sizeof(Render2D));

	sr->compositor = compositor;

	sr->strike_bank = gf_list_new();
	sr->surfaces_2D = gf_list_new();

	sr->top_effect = malloc(sizeof(RenderEffect2D));
	memset(sr->top_effect, 0, sizeof(RenderEffect2D));
	sr->top_effect->sensors = gf_list_new();
	sr->sensors = gf_list_new();
	
	/*and create main surface*/
	sr->surface = NewVisualSurface2D();
	sr->surface->GetSurfaceAccess = R2D_GetSurfaceAccess;
	sr->surface->ReleaseSurfaceAccess = R2D_ReleaseSurfaceAccess;

	sr->surface->DrawBitmap = R2D_DrawBitmap;
	sr->surface->SupportsFormat = R2D_SupportsFormat;
	sr->surface->render = sr;
	gf_list_add(sr->surfaces_2D, sr->surface);

	sr->zoom = sr->scale_x = sr->scale_y = FIX_ONE;
	vr->user_priv = sr;

	/*load options*/
	sOpt = gf_cfg_get_key(compositor->user->config, "Render2D", "DirectRender");
	if (sOpt && ! stricmp(sOpt, "yes")) 
		sr->top_effect->trav_flags |= TF_RENDER_DIRECT;
	else
		sr->top_effect->trav_flags &= ~TF_RENDER_DIRECT;
	
	sOpt = gf_cfg_get_key(compositor->user->config, "Render2D", "ScalableZoom");
	sr->scalable_zoom = (!sOpt || !stricmp(sOpt, "yes") ) ? 1 : 0;
	sOpt = gf_cfg_get_key(compositor->user->config, "Render2D", "DisableYUV");
	sr->enable_yuv_hw = (sOpt && !stricmp(sOpt, "yes") ) ? 0 : 1;
	return GF_OK;
}



void R2D_UnloadRenderer(GF_VisualRenderer *vr)
{
	Render2D *sr = (Render2D *)vr->user_priv;
	DeleteVisualSurface2D(sr->surface);
	gf_list_del(sr->sensors);
	gf_list_del(sr->surfaces_2D);
	gf_list_del(sr->strike_bank);
	effect_delete(sr->top_effect);
	free(sr);
	vr->user_priv = NULL;
}


GF_Err R2D_AllocTexture(GF_TextureHandler *hdl)
{
	if (hdl->hwtx) return GF_BAD_PARAM;
	hdl->hwtx = hdl->compositor->r2d->stencil_new(hdl->compositor->r2d, GF_STENCIL_TEXTURE);
	return GF_OK;
}

void R2D_ReleaseTexture(GF_TextureHandler *hdl)
{
	if (hdl->hwtx) hdl->compositor->r2d->stencil_delete(hdl->hwtx);
	hdl->hwtx = NULL;
}

GF_Err R2D_SetTextureData(GF_TextureHandler *hdl)
{
	Render2D *sr = (Render2D *) hdl->compositor->visual_renderer->user_priv;
	return hdl->compositor->r2d->stencil_set_texture(hdl->hwtx, hdl->data, hdl->width, hdl->height, hdl->stride, hdl->pixelformat, sr->compositor->video_out->pixel_format, 0);
}

/*no module used HW for texturing for now*/
void R2D_TextureHWReset(GF_TextureHandler *hdl)
{
	return;
}

void R2D_GraphicsReset(GF_VisualRenderer *vr)
{
}

void R2D_ReloadConfig(GF_VisualRenderer *vr)
{
	const char *sOpt;
	Render2D *sr = (Render2D *)vr->user_priv;

	gf_sr_lock(sr->compositor, 1);

	sOpt = gf_modules_get_option((GF_BaseInterface *)vr, "Render2D", "DirectRender");

	if (sOpt && !stricmp(sOpt, "yes") ) {
		sr->top_effect->trav_flags |= TF_RENDER_DIRECT;
	} else {
		sr->top_effect->trav_flags &= ~TF_RENDER_DIRECT;
	}

	sOpt = gf_modules_get_option((GF_BaseInterface *)vr, "Render2D", "ScalableZoom");
	sr->scalable_zoom = (!sOpt || !stricmp(sOpt, "yes") ) ? 1 : 0;
	sOpt = gf_modules_get_option((GF_BaseInterface *)vr, "Render2D", "DisableYUV");
	sr->enable_yuv_hw = (sOpt && !stricmp(sOpt, "yes") ) ? 0 : 1;

	sr->compositor->msg_type |= GF_SR_CFG_AR;
	sr->compositor->draw_next_frame = 1;
	gf_sr_lock(sr->compositor, 0);
}

GF_Err R2D_SetOption(GF_VisualRenderer *vr, u32 option, u32 value)
{
	Render2D *sr = (Render2D *)vr->user_priv;
	switch (option) {
	case GF_OPT_DIRECT_RENDER:
		gf_sr_lock(sr->compositor, 1);
		if (value) {
			sr->top_effect->trav_flags |= TF_RENDER_DIRECT;
		} else {
			sr->top_effect->trav_flags &= ~TF_RENDER_DIRECT;
		}
		/*force redraw*/
		gf_sr_invalidate(sr->compositor, NULL);
		gf_sr_lock(sr->compositor, 0);
		return GF_OK;
	case GF_OPT_SCALABLE_ZOOM:
		sr->scalable_zoom = value;
		/*emulate size message to force AR recompute*/
		sr->compositor->msg_type |= GF_SR_CFG_AR;
		return GF_OK;
	case GF_OPT_YUV_HARDWARE:
		sr->enable_yuv_hw = value;
		return GF_OK;
	case GF_OPT_RELOAD_CONFIG: R2D_ReloadConfig(vr); return GF_OK;
	case GF_OPT_ORIGINAL_VIEW: 
		sr->trans_x = sr->trans_y = 0;
		R2D_SetZoom(sr, FIX_ONE);
		return GF_OK;
	case GF_OPT_NAVIGATION_TYPE: 
		sr->trans_x = sr->trans_y = 0;
		R2D_SetZoom(sr, FIX_ONE);
		return GF_OK;
	case GF_OPT_NAVIGATION:
		if ((value!=GF_NAVIGATE_NONE) && (value!=GF_NAVIGATE_SLIDE)) return GF_NOT_SUPPORTED;
		sr->navigate_mode = value;
		return GF_OK;
	case GF_OPT_HEADLIGHT: return GF_NOT_SUPPORTED;
	case GF_OPT_COLLISION: return GF_NOT_SUPPORTED;
	case GF_OPT_GRAVITY: return GF_NOT_SUPPORTED;
	default: return GF_BAD_PARAM;
	}
}

u32 R2D_GetOption(GF_VisualRenderer *vr, u32 option)
{
	Render2D *sr = (Render2D *)vr->user_priv;
	switch (option) {
	case GF_OPT_DIRECT_RENDER: return (sr->top_effect->trav_flags & TF_RENDER_DIRECT) ? 1 : 0;
	case GF_OPT_SCALABLE_ZOOM: return sr->scalable_zoom;
	case GF_OPT_YUV_HARDWARE: return sr->enable_yuv_hw;
	case GF_OPT_YUV_FORMAT: return sr->enable_yuv_hw ? sr->compositor->video_out->yuv_pixel_format : 0;
	case GF_OPT_NAVIGATION_TYPE: return GF_NAVIGATE_TYPE_2D;
	case GF_OPT_NAVIGATION: return sr->navigate_mode;
	case GF_OPT_HEADLIGHT: return 0;
	case GF_OPT_COLLISION: return GF_COLLISION_NONE;
	case GF_OPT_GRAVITY: return 0;
	default: return 0;
	}
}

/*render inline scene*/
void R2D_RenderInline(GF_VisualRenderer *vr, GF_Node *inline_root, void *rs)
{
	Bool use_pm;
	u32 h, w;
	GF_Matrix2D mx_bck, mx;
	GF_SceneGraph *in_scene;
	RenderEffect2D *eff = (RenderEffect2D *)rs;

	in_scene = gf_node_get_graph(inline_root);
	use_pm = gf_sg_use_pixel_metrics(in_scene);
	if (use_pm == eff->is_pixel_metrics) {
		gf_node_render(inline_root, rs);
		return;
	}
	gf_mx2d_copy(mx_bck, eff->transform);
	/*override aspect ratio if any size info is given in the scene*/
	if (gf_sg_get_scene_size_info(in_scene, &w, &h)) {
		Fixed scale = INT2FIX( MIN(w, h) / 2);
		if (scale) eff->min_hsize = scale;
	}
	gf_mx2d_init(mx);
	/*apply meterMetrics<->pixelMetrics scale*/
	if (!use_pm) {
		gf_mx2d_add_scale(&mx, eff->min_hsize, eff->min_hsize);
	} else {
		Fixed inv_scale = gf_divfix(FIX_ONE, eff->min_hsize);
		gf_mx2d_add_scale(&mx, inv_scale, inv_scale);
	}
	eff->is_pixel_metrics = use_pm;
	gf_mx2d_add_matrix(&eff->transform, &mx);
	gf_node_render(inline_root, rs);
	eff->is_pixel_metrics = !use_pm;
	gf_mx2d_copy(eff->transform, mx_bck);
}

GF_Err R2D_GetScreenBuffer(GF_VisualRenderer *vr, GF_VideoSurface *framebuffer)
{
	Render2D *sr = (Render2D *)vr->user_priv;
	return sr->compositor->video_out->LockBackBuffer(sr->compositor->video_out, framebuffer, 1);
}

GF_Err R2D_ReleaseScreenBuffer(GF_VisualRenderer *vr, GF_VideoSurface *framebuffer)
{
	Render2D *sr = (Render2D *)vr->user_priv;
	return sr->compositor->video_out->LockBackBuffer(sr->compositor->video_out, NULL, 0);
}


GF_Err R2D_GetViewport(GF_VisualRenderer *vr, u32 viewpoint_idx, const char **outName, Bool *is_bound);
GF_Err R2D_SetViewport(GF_VisualRenderer *vr, u32 viewpoint_idx, const char *viewpoint_name);

GF_VisualRenderer *NewVisualRenderer()
{
	GF_VisualRenderer *sr;	
	sr = malloc(sizeof(GF_VisualRenderer));
	if (!sr) return NULL;
	memset(sr, 0, sizeof(GF_VisualRenderer));

	sr->LoadRenderer = R2D_LoadRenderer;
	sr->UnloadRenderer = R2D_UnloadRenderer;
	sr->GraphicsReset = R2D_GraphicsReset;
	sr->NodeChanged = R2D_NodeChanged;
	sr->NodeInit = R2D_NodeInit;
	sr->DrawScene = R2D_DrawScene;
	sr->ExecuteEvent = R2D_ExecuteEvent;
	sr->RecomputeAR = R2D_RecomputeAR;
	sr->SceneReset = R2D_SceneReset;
	sr->RenderInline = R2D_RenderInline;
	sr->AllocTexture = R2D_AllocTexture;
	sr->ReleaseTexture = R2D_ReleaseTexture;
	sr->SetTextureData = R2D_SetTextureData;
	sr->TextureHWReset = R2D_TextureHWReset;
	sr->SetOption = R2D_SetOption;
	sr->GetOption = R2D_GetOption;
	sr->GetScreenBuffer = R2D_GetScreenBuffer;
	sr->ReleaseScreenBuffer = R2D_ReleaseScreenBuffer;
	sr->GetViewpoint = R2D_GetViewport;
	sr->SetViewpoint = R2D_SetViewport;

	sr->user_priv = NULL;
	return sr;
}

#ifndef GPAC_STANDALONE_RENDER_2D

/*interface create*/
GF_BaseInterface *LoadInterface(u32 InterfaceType)
{
	GF_VisualRenderer *sr;
	if (InterfaceType != GF_RENDERER_INTERFACE) return NULL;
	
	sr = malloc(sizeof(GF_VisualRenderer));
	if (!sr) return NULL;
	memset(sr, 0, sizeof(GF_VisualRenderer));
	GF_REGISTER_MODULE_INTERFACE(sr, GF_RENDERER_INTERFACE, "GPAC 2D Renderer", "gpac distribution");

	sr->LoadRenderer = R2D_LoadRenderer;
	sr->UnloadRenderer = R2D_UnloadRenderer;
	sr->GraphicsReset = R2D_GraphicsReset;
	sr->NodeChanged = R2D_NodeChanged;
	sr->NodeInit = R2D_NodeInit;
	sr->DrawScene = R2D_DrawScene;
	sr->ExecuteEvent = R2D_ExecuteEvent;
	sr->RecomputeAR = R2D_RecomputeAR;
	sr->SceneReset = R2D_SceneReset;
	sr->RenderInline = R2D_RenderInline;
	sr->AllocTexture = R2D_AllocTexture;
	sr->ReleaseTexture = R2D_ReleaseTexture;
	sr->SetTextureData = R2D_SetTextureData;
	sr->TextureHWReset = R2D_TextureHWReset;
	sr->SetOption = R2D_SetOption;
	sr->GetOption = R2D_GetOption;
	sr->GetScreenBuffer = R2D_GetScreenBuffer;
	sr->ReleaseScreenBuffer = R2D_ReleaseScreenBuffer;
	sr->GetViewpoint = R2D_GetViewport;
	sr->SetViewpoint = R2D_SetViewport;

	sr->user_priv = NULL;
	return (GF_BaseInterface *)sr;
}


/*interface destroy*/
void ShutdownInterface(GF_BaseInterface *ifce)
{
	GF_VisualRenderer *rend = (GF_VisualRenderer *)ifce;
	if (rend->InterfaceType != GF_RENDERER_INTERFACE) return;
	if (rend->user_priv) R2D_UnloadRenderer(rend);
	free(rend);
}

/*interface query*/
Bool QueryInterface(u32 InterfaceType)
{
	if (InterfaceType == GF_RENDERER_INTERFACE) return 1;
	return 0;
}

#endif

