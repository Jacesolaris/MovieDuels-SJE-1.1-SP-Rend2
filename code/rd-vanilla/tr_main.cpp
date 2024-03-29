/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// tr_main.c -- main control flow for each frame

#include "../server/exe_headers.h"

#include "tr_local.h"

#if !defined(G2_H_INC)
#include "../ghoul2/G2.h"
#endif

trGlobals_t		tr;

static float	s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};

refimport_t ri;

// entities that will have procedurally generated surfaces will just
// point at this for their sorting surface
surfaceType_t	entitySurface = SF_ENTITY;

/*
=================
R_CullLocalBox

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
int R_CullLocalBox(const vec3_t bounds[2]) {
	int		i;
	vec3_t	transformed[8]{};
	float	dists[8]{};
	vec3_t	v{};
	int back;

	if (r_nocull->integer == 1) {
		return CULL_CLIP;
	}

	// transform into world space
	for (i = 0; i < 8; i++) {
		v[0] = bounds[i & 1][0];
		v[1] = bounds[i >> 1 & 1][1];
		v[2] = bounds[i >> 2 & 1][2];

		VectorCopy(tr.ori.origin, transformed[i]);
		VectorMA(transformed[i], v[0], tr.ori.axis[0], transformed[i]);
		VectorMA(transformed[i], v[1], tr.ori.axis[1], transformed[i]);
		VectorMA(transformed[i], v[2], tr.ori.axis[2], transformed[i]);
	}

	// check against frustum planes
	int anyBack = 0;
	for (i = 0; i < 5; i++) {
		const cplane_t* frust = &tr.viewParms.frustum[i];

		int front = back = 0;
		for (int j = 0; j < 8; j++) {
			dists[j] = DotProduct(transformed[j], frust->normal);
			if (dists[j] > frust->dist) {
				front = 1;
				if (back) {
					break;		// a point is in front
				}
			}
			else {
				back = 1;
			}
		}
		if (!front) {
			// all points were behind one of the planes
			return CULL_OUT;
		}
		anyBack |= back;
	}

	if (!anyBack) {
		return CULL_IN;		// completely inside frustum
	}

	return CULL_CLIP;		// partially clipped
}

/*
** R_CullLocalPointAndRadius
*/
int R_CullLocalPointAndRadius(const vec3_t pt, const float radius)
{
	vec3_t transformed;

	R_LocalPointToWorld(pt, transformed);

	return R_CullPointAndRadius(transformed, radius);
}

/*
** R_CullPointAndRadius
*/
int R_CullPointAndRadius(const vec3_t pt, float radius)
{
	float	dist;
	cplane_t* frust;
	qboolean might_be_clipped = qfalse;

	if (r_nocull->integer == 1) {
		return CULL_CLIP;
	}

	// check against frustum planes
#ifdef JK2_MODE
	// They used 4 frustrum planes in JK2, and 5 in JKA --eez
	for (i = 0; i < 4; i++)
	{
		frust = &tr.viewParms.frustum[i];

		dist = DotProduct(pt, frust->normal) - frust->dist;
		if (dist < -radius)
		{
			return CULL_OUT;
		}
		else if (dist <= radius)
		{
			might_be_clipped = qtrue;
		}
	}
#else
	for (auto& i : tr.viewParms.frustum)
	{
		frust = &i;

		dist = DotProduct(pt, frust->normal) - frust->dist;
		if (dist < -radius)
		{
			return CULL_OUT;
		}
		if (dist <= radius)
		{
			might_be_clipped = qtrue;
		}
	}
#endif

	if (might_be_clipped)
	{
		return CULL_CLIP;
	}

	return CULL_IN;		// completely inside frustum
}

/*
=================
R_LocalNormalToWorld

=================
*/
void R_LocalNormalToWorld(const vec3_t local, vec3_t world) {
	world[0] = local[0] * tr.ori.axis[0][0] + local[1] * tr.ori.axis[1][0] + local[2] * tr.ori.axis[2][0];
	world[1] = local[0] * tr.ori.axis[0][1] + local[1] * tr.ori.axis[1][1] + local[2] * tr.ori.axis[2][1];
	world[2] = local[0] * tr.ori.axis[0][2] + local[1] * tr.ori.axis[1][2] + local[2] * tr.ori.axis[2][2];
}

/*
=================
R_LocalPointToWorld

=================
*/
void R_LocalPointToWorld(const vec3_t local, vec3_t world) {
	world[0] = local[0] * tr.ori.axis[0][0] + local[1] * tr.ori.axis[1][0] + local[2] * tr.ori.axis[2][0] + tr.ori.origin[0];
	world[1] = local[0] * tr.ori.axis[0][1] + local[1] * tr.ori.axis[1][1] + local[2] * tr.ori.axis[2][1] + tr.ori.origin[1];
	world[2] = local[0] * tr.ori.axis[0][2] + local[1] * tr.ori.axis[1][2] + local[2] * tr.ori.axis[2][2] + tr.ori.origin[2];
}

float preTransEntMatrix[16];

void R_InvertMatrix(const float* sourcemat, float* destmat)
{
	int i, j, temp = 0;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
		{
			destmat[j * 4 + i] = sourcemat[temp++];
		}
	}
	for (i = 0; i < 3; i++)
	{
		temp = i * 4;
		destmat[temp + 3] = 0;		// destmat[destmat[i][3]=0;
		for (j = 0; j < 3; j++)
		{
			destmat[temp + 3] -= destmat[temp + j] * sourcemat[j * 4 + 3];		// dest->matrix[i][3]-=dest->matrix[i][j]*src->matrix[j][3];
		}
	}
}

/*
=================
R_WorldNormalToEntity

=================
*/
void R_WorldNormalToEntity(const vec3_t worldvec, vec3_t entvec)
{
	entvec[0] = -worldvec[0] * preTransEntMatrix[0] - worldvec[1] * preTransEntMatrix[4] + worldvec[2] * preTransEntMatrix[8];
	entvec[1] = -worldvec[0] * preTransEntMatrix[1] - worldvec[1] * preTransEntMatrix[5] + worldvec[2] * preTransEntMatrix[9];
	entvec[2] = -worldvec[0] * preTransEntMatrix[2] - worldvec[1] * preTransEntMatrix[6] + worldvec[2] * preTransEntMatrix[10];
}

/*
=================
R_WorldPointToEntity

=================
*/
/*void R_WorldPointToEntity (vec3_t worldvec, vec3_t entvec)
{
	entvec[0] = worldvec[0] * preTransEntMatrix[0] + worldvec[1] * preTransEntMatrix[4] + worldvec[2] * preTransEntMatrix[8]+preTransEntMatrix[12];
	entvec[1] = worldvec[0] * preTransEntMatrix[1] + worldvec[1] * preTransEntMatrix[5] + worldvec[2] * preTransEntMatrix[9]+preTransEntMatrix[13];
	entvec[2] = worldvec[0] * preTransEntMatrix[2] + worldvec[1] * preTransEntMatrix[6] + worldvec[2] * preTransEntMatrix[10]+preTransEntMatrix[14];
}
*/

/*
=================
R_WorldToLocal

=================
*/
void R_WorldToLocal(vec3_t world, vec3_t local) {
	local[0] = DotProduct(world, tr.ori.axis[0]);
	local[1] = DotProduct(world, tr.ori.axis[1]);
	local[2] = DotProduct(world, tr.ori.axis[2]);
}

/*
==========================
R_TransformModelToClip

==========================
*/
void R_TransformModelToClip(const vec3_t src, const float* model_matrix, const float* projection_matrix,
	vec4_t eye, vec4_t dst) {
	int i;

	for (i = 0; i < 4; i++) {
		eye[i] =
			src[0] * model_matrix[i + 0 * 4] +
			src[1] * model_matrix[i + 1 * 4] +
			src[2] * model_matrix[i + 2 * 4] +
			1 * model_matrix[i + 3 * 4];
	}

	for (i = 0; i < 4; i++) {
		dst[i] =
			eye[0] * projection_matrix[i + 0 * 4] +
			eye[1] * projection_matrix[i + 1 * 4] +
			eye[2] * projection_matrix[i + 2 * 4] +
			eye[3] * projection_matrix[i + 3 * 4];
	}
}

/*
==========================
R_TransformClipToWindow

==========================
*/
void R_TransformClipToWindow(const vec4_t clip, const viewParms_t* view, vec4_t normalized, vec4_t window) {
	normalized[0] = clip[0] / clip[3];
	normalized[1] = clip[1] / clip[3];
	normalized[2] = (clip[2] + clip[3]) / (2 * clip[3]);

	window[0] = 0.5 * (1.0 + normalized[0]) * view->viewportWidth;
	window[1] = 0.5 * (1.0 + normalized[1]) * view->viewportHeight;
	window[2] = normalized[2];

	window[0] = static_cast<int>(window[0] + 0.5);
	window[1] = static_cast<int>(window[1] + 0.5);
}

/*
==========================
myGlMultMatrix

==========================
*/
void myGlMultMatrix(const float* a, const float* b, float* out) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			out[i * 4 + j] =
				a[i * 4 + 0] * b[0 * 4 + j]
				+ a[i * 4 + 1] * b[1 * 4 + j]
				+ a[i * 4 + 2] * b[2 * 4 + j]
				+ a[i * 4 + 3] * b[3 * 4 + j];
		}
	}
}

/*
=================
R_RotateForEntity

Generates an orientation for an entity and viewParms
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateForEntity(const trRefEntity_t* ent, const viewParms_t* view_parms,
	orientationr_t* ori) {
	//	float	glMatrix[16];
	vec3_t	delta;
	float	axis_length;

	if (ent->e.reType != RT_MODEL) {
		*ori = view_parms->world;
		return;
	}

	VectorCopy(ent->e.origin, ori->origin);

	VectorCopy(ent->e.axis[0], ori->axis[0]);
	VectorCopy(ent->e.axis[1], ori->axis[1]);
	VectorCopy(ent->e.axis[2], ori->axis[2]);

	preTransEntMatrix[0] = ori->axis[0][0];
	preTransEntMatrix[4] = ori->axis[1][0];
	preTransEntMatrix[8] = ori->axis[2][0];
	preTransEntMatrix[12] = ori->origin[0];

	preTransEntMatrix[1] = ori->axis[0][1];
	preTransEntMatrix[5] = ori->axis[1][1];
	preTransEntMatrix[9] = ori->axis[2][1];
	preTransEntMatrix[13] = ori->origin[1];

	preTransEntMatrix[2] = ori->axis[0][2];
	preTransEntMatrix[6] = ori->axis[1][2];
	preTransEntMatrix[10] = ori->axis[2][2];
	preTransEntMatrix[14] = ori->origin[2];

	preTransEntMatrix[3] = 0;
	preTransEntMatrix[7] = 0;
	preTransEntMatrix[11] = 0;
	preTransEntMatrix[15] = 1;

	myGlMultMatrix(preTransEntMatrix, view_parms->world.model_matrix, ori->model_matrix);

	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract(view_parms->ori.origin, ori->origin, delta);

	// compensate for scale in the axes if necessary
	if (ent->e.nonNormalizedAxes) {
		axis_length = VectorLength(ent->e.axis[0]);
		if (!axis_length) {
			axis_length = 0;
		}
		else {
			axis_length = 1.0 / axis_length;
		}
	}
	else {
		axis_length = 1.0;
	}

	ori->viewOrigin[0] = DotProduct(delta, ori->axis[0]) * axis_length;
	ori->viewOrigin[1] = DotProduct(delta, ori->axis[1]) * axis_length;
	ori->viewOrigin[2] = DotProduct(delta, ori->axis[2]) * axis_length;
}

/*
=================
R_RotateForViewer

Sets up the modelview matrix for a given viewParm
=================
*/
void R_RotateForViewer()
{
	float	viewer_matrix[16]{};
	vec3_t	origin;

	memset(&tr.ori, 0, sizeof tr.ori);
	tr.ori.axis[0][0] = 1;
	tr.ori.axis[1][1] = 1;
	tr.ori.axis[2][2] = 1;
	VectorCopy(tr.viewParms.ori.origin, tr.ori.viewOrigin);

	// transform by the camera placement
	VectorCopy(tr.viewParms.ori.origin, origin);

	viewer_matrix[0] = tr.viewParms.ori.axis[0][0];
	viewer_matrix[4] = tr.viewParms.ori.axis[0][1];
	viewer_matrix[8] = tr.viewParms.ori.axis[0][2];
	viewer_matrix[12] = -origin[0] * viewer_matrix[0] + -origin[1] * viewer_matrix[4] + -origin[2] * viewer_matrix[8];

	viewer_matrix[1] = tr.viewParms.ori.axis[1][0];
	viewer_matrix[5] = tr.viewParms.ori.axis[1][1];
	viewer_matrix[9] = tr.viewParms.ori.axis[1][2];
	viewer_matrix[13] = -origin[0] * viewer_matrix[1] + -origin[1] * viewer_matrix[5] + -origin[2] * viewer_matrix[9];

	viewer_matrix[2] = tr.viewParms.ori.axis[2][0];
	viewer_matrix[6] = tr.viewParms.ori.axis[2][1];
	viewer_matrix[10] = tr.viewParms.ori.axis[2][2];
	viewer_matrix[14] = -origin[0] * viewer_matrix[2] + -origin[1] * viewer_matrix[6] + -origin[2] * viewer_matrix[10];

	viewer_matrix[3] = 0;
	viewer_matrix[7] = 0;
	viewer_matrix[11] = 0;
	viewer_matrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	myGlMultMatrix(viewer_matrix, s_flipMatrix, tr.ori.model_matrix);

	tr.viewParms.world = tr.ori;
}

/*
** SetFarClip
*/
static void SetFarClip()
{
	float	farthest_corner_distance = 0;

	// if not rendering the world (icons, menus, etc)
	// set a 2k far clip plane
	if (tr.refdef.rdflags & RDF_NOWORLDMODEL) {
		tr.viewParms.zFar = 2048;
		return;
	}

	//
	// set far clipping planes dynamically
	//
	for (int i = 0; i < 8; i++)
	{
		vec3_t v{};

		if (i & 1)
		{
			v[0] = tr.viewParms.visBounds[0][0];
		}
		else
		{
			v[0] = tr.viewParms.visBounds[1][0];
		}

		if (i & 2)
		{
			v[1] = tr.viewParms.visBounds[0][1];
		}
		else
		{
			v[1] = tr.viewParms.visBounds[1][1];
		}

		if (i & 4)
		{
			v[2] = tr.viewParms.visBounds[0][2];
		}
		else
		{
			v[2] = tr.viewParms.visBounds[1][2];
		}

		const float distance = DistanceSquared(tr.viewParms.ori.origin, v);

		if (distance > farthest_corner_distance)
		{
			farthest_corner_distance = distance;
		}
	}
	// Bring in the zFar to the distanceCull distance
	// The sky renders at zFar so need to move it out a little
	// ...and make sure there is a minimum zfar to prevent problems
	tr.viewParms.zFar = Com_Clamp(2048.0f, tr.distanceCull * 1.732, sqrtf(farthest_corner_distance));
}

/*
===============
R_SetupProjection
===============
*/
void R_SetupProjection() {
	// dynamically compute far clip plane distance
	SetFarClip();

	//
	// set up projection matrix
	//
	const float z_near = r_znear->value;
	const float z_far = tr.viewParms.zFar;

	const float ymax = z_near * tan(tr.refdef.fov_y * M_PI / 360.0f);
	const float ymin = -ymax;

	const float xmax = z_near * tan(tr.refdef.fov_x * M_PI / 360.0f);
	const float xmin = -xmax;

	const float width = xmax - xmin;
	const float height = ymax - ymin;
	const float depth = z_far - z_near;

	tr.viewParms.projectionMatrix[0] = 2 * z_near / width;
	tr.viewParms.projectionMatrix[4] = 0;
	tr.viewParms.projectionMatrix[8] = (xmax + xmin) / width;	// normally 0
	tr.viewParms.projectionMatrix[12] = 0;

	tr.viewParms.projectionMatrix[1] = 0;
	tr.viewParms.projectionMatrix[5] = 2 * z_near / height;
	tr.viewParms.projectionMatrix[9] = (ymax + ymin) / height;	// normally 0
	tr.viewParms.projectionMatrix[13] = 0;

	tr.viewParms.projectionMatrix[2] = 0;
	tr.viewParms.projectionMatrix[6] = 0;
	tr.viewParms.projectionMatrix[10] = -(z_far + z_near) / depth;
	tr.viewParms.projectionMatrix[14] = -2 * z_far * z_near / depth;

	tr.viewParms.projectionMatrix[3] = 0;
	tr.viewParms.projectionMatrix[7] = 0;
	tr.viewParms.projectionMatrix[11] = -1;
	tr.viewParms.projectionMatrix[15] = 0;
}

/*
=================
R_SetupFrustum

Setup that culling frustum planes for the current view
=================
*/
void R_SetupFrustum() {
	float ang = tr.viewParms.fovX / 180 * M_PI * 0.5;
	float xs = sin(ang);
	float xc = cos(ang);

	VectorScale(tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[0].normal);
	VectorMA(tr.viewParms.frustum[0].normal, xc, tr.viewParms.ori.axis[1], tr.viewParms.frustum[0].normal);

	VectorScale(tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[1].normal);
	VectorMA(tr.viewParms.frustum[1].normal, -xc, tr.viewParms.ori.axis[1], tr.viewParms.frustum[1].normal);

	ang = tr.viewParms.fovY / 180 * M_PI * 0.5;
	xs = sin(ang);
	xc = cos(ang);

	VectorScale(tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[2].normal);
	VectorMA(tr.viewParms.frustum[2].normal, xc, tr.viewParms.ori.axis[2], tr.viewParms.frustum[2].normal);

	VectorScale(tr.viewParms.ori.axis[0], xs, tr.viewParms.frustum[3].normal);
	VectorMA(tr.viewParms.frustum[3].normal, -xc, tr.viewParms.ori.axis[2], tr.viewParms.frustum[3].normal);

	// this is the far plane
	VectorScale(tr.viewParms.ori.axis[0], -1.0f, tr.viewParms.frustum[4].normal);

	for (int i = 0; i < 5; i++) {
		tr.viewParms.frustum[i].type = PLANE_NON_AXIAL;
		tr.viewParms.frustum[i].dist = DotProduct(tr.viewParms.ori.origin, tr.viewParms.frustum[i].normal);
		if (i == 4)
		{
			// far plane does not go through the view point, it goes alot farther..
			tr.viewParms.frustum[i].dist -= tr.distanceCull * 1.02f; // a little slack so we don't cull stuff
		}
		SetPlaneSignbits(&tr.viewParms.frustum[i]);
	}
}

/*
=================
R_MirrorPoint
=================
*/
void R_MirrorPoint(vec3_t in, const orientation_t* surface, const orientation_t* camera, vec3_t out) {
	vec3_t	local;
	vec3_t	transformed;

	VectorSubtract(in, surface->origin, local);

	VectorClear(transformed);
	for (int i = 0; i < 3; i++) {
		const float d = DotProduct(local, surface->axis[i]);
		VectorMA(transformed, d, camera->axis[i], transformed);
	}

	VectorAdd(transformed, camera->origin, out);
}

void R_MirrorVector(vec3_t in, const orientation_t* surface, const orientation_t* camera, vec3_t out) {
	VectorClear(out);
	for (int i = 0; i < 3; i++) {
		const float d = DotProduct(in, surface->axis[i]);
		VectorMA(out, d, camera->axis[i], out);
	}
}

/*
=============
R_PlaneForSurface
=============
*/
void R_PlaneForSurface(surfaceType_t* surf_type, cplane_t* plane) {
	srfTriangles_t* tri;
	srfGridMesh_t* grid;
	srfPoly_t* poly;
	drawVert_t* v1, * v2, * v3;
	vec4_t			plane4;

	if (!surf_type) {
		memset(plane, 0, sizeof * plane);
		plane->normal[0] = 1;
		return;
	}
	switch (*surf_type) {
	case SF_FACE:
		*plane = reinterpret_cast<srfSurfaceFace_t*>(surf_type)->plane;
		return;
	case SF_TRIANGLES:
		tri = reinterpret_cast<srfTriangles_t*>(surf_type);
		v1 = tri->verts + tri->indexes[0];
		v2 = tri->verts + tri->indexes[1];
		v3 = tri->verts + tri->indexes[2];
		PlaneFromPoints(plane4, v1->xyz, v2->xyz, v3->xyz);
		VectorCopy(plane4, plane->normal);
		plane->dist = plane4[3];
		return;
	case SF_POLY:
		poly = reinterpret_cast<srfPoly_t*>(surf_type);
		PlaneFromPoints(plane4, poly->verts[0].xyz, poly->verts[1].xyz, poly->verts[2].xyz);
		VectorCopy(plane4, plane->normal);
		plane->dist = plane4[3];
		return;
	case SF_GRID:
		grid = reinterpret_cast<srfGridMesh_t*>(surf_type);
		v1 = &grid->verts[0];
		v2 = &grid->verts[1];
		v3 = &grid->verts[2];
		PlaneFromPoints(plane4, v3->xyz, v2->xyz, v1->xyz);
		VectorCopy(plane4, plane->normal);
		plane->dist = plane4[3];
		return;
	default:
		memset(plane, 0, sizeof * plane);
		plane->normal[0] = 1;
	}
}

/*
=================
R_GetPortalOrientation

entityNum is the entity that the portal surface is a part of, which may
be moving and rotating.

Returns qtrue if it should be mirrored
=================
*/
qboolean R_GetPortalOrientations(const drawSurf_t* draw_surf, const int entityNum,
	orientation_t* surface, orientation_t* camera,
	vec3_t pvs_origin, qboolean* mirror) {
	cplane_t	original_plane, plane{};

	// create plane axis for the portal we are seeing
	R_PlaneForSurface(draw_surf->surface, &original_plane);

	// rotate the plane if necessary
	if (entityNum != REFENTITYNUM_WORLD) {
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity(tr.currentEntity, &tr.viewParms, &tr.ori);

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld(original_plane.normal, plane.normal);
		plane.dist = original_plane.dist + DotProduct(plane.normal, tr.ori.origin);

		// translate the original plane
		original_plane.dist = original_plane.dist + DotProduct(original_plane.normal, tr.ori.origin);
	}
	else {
		plane = original_plane;
	}

	VectorCopy(plane.normal, surface->axis[0]);
	PerpendicularVector(surface->axis[1], surface->axis[0]);
	CrossProduct(surface->axis[0], surface->axis[1], surface->axis[2]);

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for (int i = 0; i < tr.refdef.num_entities; i++) {
		vec3_t transformed;
		trRefEntity_t* e = &tr.refdef.entities[i];
		if (e->e.reType != RT_PORTALSURFACE) {
			continue;
		}

		float d = DotProduct(e->e.origin, original_plane.normal) - original_plane.dist;
		if (d > 64 || d < -64) {
			continue;
		}

		// get the pvsOrigin from the entity
		VectorCopy(e->e.oldorigin, pvs_origin);

		// if the entity is just a mirror, don't use as a camera point
		if (e->e.oldorigin[0] == e->e.origin[0] &&
			e->e.oldorigin[1] == e->e.origin[1] &&
			e->e.oldorigin[2] == e->e.origin[2]) {
			VectorScale(plane.normal, plane.dist, surface->origin);
			VectorCopy(surface->origin, camera->origin);
			VectorSubtract(vec3_origin, surface->axis[0], camera->axis[0]);
			VectorCopy(surface->axis[1], camera->axis[1]);
			VectorCopy(surface->axis[2], camera->axis[2]);

			*mirror = qtrue;
			return qtrue;
		}

		// project the origin onto the surface plane to get
		// an origin point we can rotate around
		d = DotProduct(e->e.origin, plane.normal) - plane.dist;
		VectorMA(e->e.origin, -d, surface->axis[0], surface->origin);

		// now get the camera origin and orientation
		VectorCopy(e->e.oldorigin, camera->origin);
		AxisCopy(e->e.axis, camera->axis);
		VectorSubtract(vec3_origin, camera->axis[0], camera->axis[0]);
		VectorSubtract(vec3_origin, camera->axis[1], camera->axis[1]);

		// optionally rotate
		if (e->e.frame) {
			// continuous rotate
			d = tr.refdef.time / 1000.0f * e->e.frame;
			VectorCopy(camera->axis[1], transformed);
			RotatePointAroundVector(camera->axis[1], camera->axis[0], transformed, d);
			CrossProduct(camera->axis[0], camera->axis[1], camera->axis[2]);
		}
		else if (e->e.skinNum) {
			// bobbing rotate
			//d = 4 * sin( tr.refdef.time * 0.003 );
			d = e->e.skinNum;
			VectorCopy(camera->axis[1], transformed);
			RotatePointAroundVector(camera->axis[1], camera->axis[0], transformed, d);
			CrossProduct(camera->axis[0], camera->axis[1], camera->axis[2]);
		}
		*mirror = qfalse;
		return qtrue;
	}

	// if we didn't locate a portal entity, don't render anything.
	// We don't want to just treat it as a mirror, because without a
	// portal entity the server won't have communicated a proper entity set
	// in the snapshot

	// unfortunately, with local movement prediction it is easily possible
	// to see a surface before the server has communicated the matching
	// portal surface entity, so we don't want to print anything here...

	//ri.Printf( PRINT_ALL, "Portal surface without a portal entity\n" );

	return qfalse;
}

static qboolean IsMirror(const drawSurf_t* draw_surf, const int entityNum)
{
	cplane_t	original_plane, plane{};

	// create plane axis for the portal we are seeing
	R_PlaneForSurface(draw_surf->surface, &original_plane);

	// rotate the plane if necessary
	if (entityNum != REFENTITYNUM_WORLD)
	{
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity(tr.currentEntity, &tr.viewParms, &tr.ori);

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld(original_plane.normal, plane.normal);
		plane.dist = original_plane.dist + DotProduct(plane.normal, tr.ori.origin);

		// translate the original plane
		original_plane.dist = original_plane.dist + DotProduct(original_plane.normal, tr.ori.origin);
	}
	else
	{
		plane = original_plane;
	}

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for (int i = 0; i < tr.refdef.num_entities; i++)
	{
		const trRefEntity_t* e = &tr.refdef.entities[i];
		if (e->e.reType != RT_PORTALSURFACE) {
			continue;
		}

		const float d = DotProduct(e->e.origin, original_plane.normal) - original_plane.dist;
		if (d > 64 || d < -64) {
			continue;
		}

		// if the entity is just a mirror, don't use as a camera point
		if (e->e.oldorigin[0] == e->e.origin[0] &&
			e->e.oldorigin[1] == e->e.origin[1] &&
			e->e.oldorigin[2] == e->e.origin[2])
		{
			return qtrue;
		}

		return qfalse;
	}
	return qfalse;
}

/*
** SurfIsOffscreen
**
** Determines if a surface is completely offscreen.
*/
static qboolean SurfIsOffscreen(const drawSurf_t* draw_surf) {
	float shortest = 1000000000;
	int entityNum;
	shader_t* shader;
	int		fogNum;
	int dlighted;
	int i;
	unsigned int point_or = 0;
	unsigned int point_and = static_cast<unsigned>(~0);

	R_RotateForViewer();

	R_DecomposeSort(draw_surf->sort, &entityNum, &shader, &fogNum, &dlighted);
	RB_BeginSurface(shader, fogNum);
	rb_surfaceTable[*draw_surf->surface](draw_surf->surface);

	assert(tess.numVertexes < 128);

	for (i = 0; i < tess.numVertexes; i++)
	{
		vec4_t eye;
		vec4_t clip;
		unsigned int point_flags = 0;

		R_TransformModelToClip(tess.xyz[i], tr.ori.model_matrix, tr.viewParms.projectionMatrix, eye, clip);

		for (int j = 0; j < 3; j++)
		{
			if (clip[j] >= clip[3])
			{
				point_flags |= 1 << j * 2;
			}
			else if (clip[j] <= -clip[3])
			{
				point_flags |= (1 << (j * 2 + 1));
			}
		}
		point_and &= point_flags;
		point_or |= point_flags;
	}

	// trivially reject
	if (point_and)
	{
		return qtrue;
	}

	// determine if this surface is backfaced and also determine the distance
	// to the nearest vertex so we can cull based on portal range.  Culling
	// based on vertex distance isn't 100% correct (we should be checking for
	// range to the surface), but it's good enough for the types of portals
	// we have in the game right now.
	int num_triangles = tess.numIndexes / 3;

	for (i = 0; i < tess.numIndexes; i += 3)
	{
		vec3_t normal;
		float dot;

		VectorSubtract(tess.xyz[tess.indexes[i]], tr.viewParms.ori.origin, normal);

		const float len = VectorLengthSquared(normal);			// lose the sqrt
		if (len < shortest)
		{
			shortest = len;
		}

		if ((dot = DotProduct(normal, tess.normal[tess.indexes[i]])) >= 0)
		{
			num_triangles--;
		}
	}
	if (!num_triangles)
	{
		return qtrue;
	}

	// mirrors can early out at this point, since we don't do a fade over distance
	// with them (although we could)
	if (IsMirror(draw_surf, entityNum))
	{
		return qfalse;
	}

	if (shortest > tess.shader->portalRange * tess.shader->portalRange)
	{
		return qtrue;
	}

	return qfalse;
}

/*
========================
R_MirrorViewBySurface

Returns qtrue if another view has been rendered
========================
*/
int	recursivePortalCount;
qboolean R_MirrorViewBySurface(drawSurf_t* draw_surf, int entityNum)
{
	viewParms_t		new_parms;
	viewParms_t		old_parms;
	orientation_t	surface, camera;

	// don't recursively mirror
	if (tr.viewParms.is_portal)
	{
		ri.Printf(PRINT_DEVELOPER, "WARNING: recursive mirror/portal found\n");
		return qfalse;
	}

	if (r_noportals->integer || r_fastsky->integer) {
		return qfalse;
	}

	// trivially reject portal/mirror
	if (SurfIsOffscreen(draw_surf)) {
		return qfalse;
	}

	// save old viewParms so we can return to it after the mirror view
	old_parms = tr.viewParms;

	new_parms = tr.viewParms;
	new_parms.is_portal = qtrue;
	if (!R_GetPortalOrientations(draw_surf, entityNum, &surface, &camera,
		new_parms.pvsOrigin, &new_parms.isMirror)) {
		return qfalse;		// bad portal, no portalentity
	}

	R_MirrorPoint(old_parms.ori.origin, &surface, &camera, new_parms.ori.origin);

	VectorSubtract(vec3_origin, camera.axis[0], new_parms.portalPlane.normal);
	new_parms.portalPlane.dist = DotProduct(camera.origin, new_parms.portalPlane.normal);

	R_MirrorVector(old_parms.ori.axis[0], &surface, &camera, new_parms.ori.axis[0]);
	R_MirrorVector(old_parms.ori.axis[1], &surface, &camera, new_parms.ori.axis[1]);
	R_MirrorVector(old_parms.ori.axis[2], &surface, &camera, new_parms.ori.axis[2]);

	// OPTIMIZE: restrict the viewport on the mirrored view

	// render the mirror view
	R_RenderView(&new_parms);

	tr.viewParms = old_parms;

	return qtrue;
}

/*
=================
R_SpriteFogNum

See if a sprite is inside a fog volume
=================
*/
int R_SpriteFogNum(const trRefEntity_t* ent) {
	if (tr.refdef.rdflags & RDF_NOWORLDMODEL) {
		return 0;
	}

	if (tr.refdef.doLAGoggles)
	{
		return tr.world->numfogs;
	}

	int partial_fog = 0;
	for (int i = 1; i < tr.world->numfogs; i++) {
		const fog_t* fog = &tr.world->fogs[i];
		if (ent->e.origin[0] - ent->e.radius >= fog->bounds[0][0]
			&& ent->e.origin[0] + ent->e.radius <= fog->bounds[1][0]
			&& ent->e.origin[1] - ent->e.radius >= fog->bounds[0][1]
			&& ent->e.origin[1] + ent->e.radius <= fog->bounds[1][1]
			&& ent->e.origin[2] - ent->e.radius >= fog->bounds[0][2]
			&& ent->e.origin[2] + ent->e.radius <= fog->bounds[1][2])
		{//totally inside it
			return i;
		}
		if ((ent->e.origin[0] - ent->e.radius >= fog->bounds[0][0] && ent->e.origin[1] - ent->e.radius >= fog->bounds[0][1] && ent->e.origin[2] - ent->e.radius >= fog->bounds[0][2] &&
			ent->e.origin[0] - ent->e.radius <= fog->bounds[1][0] && ent->e.origin[1] - ent->e.radius <= fog->bounds[1][1] && ent->e.origin[2] - ent->e.radius <= fog->bounds[1][2]) ||
			(ent->e.origin[0] + ent->e.radius >= fog->bounds[0][0] && ent->e.origin[1] + ent->e.radius >= fog->bounds[0][1] && ent->e.origin[2] + ent->e.radius >= fog->bounds[0][2] &&
				ent->e.origin[0] + ent->e.radius <= fog->bounds[1][0] && ent->e.origin[1] + ent->e.radius <= fog->bounds[1][1] && ent->e.origin[2] + ent->e.radius <= fog->bounds[1][2]))
		{
			//partially inside it
			if (tr.refdef.fogIndex == i || R_FogParmsMatch(tr.refdef.fogIndex, i))
			{//take new one only if it's the same one that the viewpoint is in
				return i;
			}
			if (!partial_fog)
			{//first partialFog
				partial_fog = i;
			}
		}
	}

	return partial_fog;
}

/*
==========================================================================================

DRAWSURF SORTING

==========================================================================================
*/

/*
===============
R_Radix
===============
*/
static QINLINE void R_Radix(const int byte, const int size, drawSurf_t* source, drawSurf_t* dest)
{
	int           count[256] = { 0 };
	int           index[256]{};
	int           i;

	unsigned char* sort_key = reinterpret_cast<unsigned char*>(&source[0].sort) + byte;
	for (const unsigned char* end = sort_key + size * sizeof(drawSurf_t); sort_key < end; sort_key += sizeof(drawSurf_t))
		++count[*sort_key];

	index[0] = 0;

	for (i = 1; i < 256; ++i)
		index[i] = index[i - 1] + count[i - 1];

	sort_key = reinterpret_cast<unsigned char*>(&source[0].sort) + byte;
	for (i = 0; i < size; ++i, sort_key += sizeof(drawSurf_t))
		dest[index[*sort_key]++] = source[i];
}

/*
===============
R_RadixSort

Radix sort with 4 byte size buckets
===============
*/
static void R_RadixSort(drawSurf_t* source, int size)
{
	static drawSurf_t scratch[MAX_DRAWSURFS];
#ifdef Q3_LITTLE_ENDIAN
	R_Radix(0, size, source, scratch);
	R_Radix(1, size, scratch, source);
	R_Radix(2, size, source, scratch);
	R_Radix(3, size, scratch, source);
#else
	R_Radix(3, size, source, scratch);
	R_Radix(2, size, scratch, source);
	R_Radix(1, size, source, scratch);
	R_Radix(0, size, scratch, source);
#endif //Q3_LITTLE_ENDIAN
}

//==========================================================================================

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf(surfaceType_t* surface, const shader_t* shader, int fogIndex, const int dlightMap)
{
	int			index;

	// instead of checking for overflow, we just mask the index
	// so it wraps around
	index = tr.refdef.numDrawSurfs & DRAWSURF_MASK;

	if (tr.refdef.doLAGoggles)
	{
		fogIndex = tr.world->numfogs;
	}

	if ((shader->surfaceFlags & SURF_FORCESIGHT) && !(tr.refdef.rdflags & RDF_ForceSightOn))
	{	//if shader is only seen with ForceSight and we don't have ForceSight on, then don't draw
		return;
	}

	// the sort data is packed into a single 32 bit value so it can be
	// compared quickly during the qsorting process
	tr.refdef.drawSurfs[index].sort = (shader->sortedIndex << QSORT_SHADERNUM_SHIFT) | tr.shiftedEntityNum | (fogIndex << QSORT_FOGNUM_SHIFT) | (int)dlightMap;
	tr.refdef.drawSurfs[index].surface = (surfaceType_t*)surface;
	tr.refdef.numDrawSurfs++;
}

/*
=================
R_DecomposeSort
=================
*/
void R_DecomposeSort(const unsigned sort, int* entityNum, shader_t** shader,
	int* fogNum, int* dlightMap) {
	*fogNum = sort >> QSORT_FOGNUM_SHIFT & 31;
	*shader = tr.sortedShaders[sort >> QSORT_SHADERNUM_SHIFT & MAX_SHADERS - 1];
	*entityNum = sort >> QSORT_REFENTITYNUM_SHIFT & REFENTITYNUM_MASK;
	*dlightMap = sort & 3;
}

/*
=================
R_SortDrawSurfs
=================
*/
void R_SortDrawSurfs(drawSurf_t* drawSurfs, int numDrawSurfs) {
	shader_t* shader;
	int				fogNum;
	int				entityNum;
	int				dlighted;

	// it is possible for some views to not have any surfaces
	if (numDrawSurfs < 1) {
		// we still need to add it for hyperspace cases
		R_AddDrawSurfCmd(drawSurfs, numDrawSurfs);
		return;
	}

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
	// wrapped around in the buffer and we will be missing
	// the first surfaces, not the last ones
	if (numDrawSurfs > MAX_DRAWSURFS) {
		numDrawSurfs = MAX_DRAWSURFS;
	}

	// sort the drawsurfs by sort type, then orientation, then shader
	R_RadixSort(drawSurfs, numDrawSurfs);

	// check for any pass through drawing, which
	// may cause another view to be rendered first
	for (int i = 0; i < numDrawSurfs; i++) {
		R_DecomposeSort((drawSurfs + i)->sort, &entityNum, &shader, &fogNum, &dlighted);

		if (shader->sort > SS_PORTAL)
		{
			break;
		}

		// no shader should ever have this sort type
		if (shader->sort == SS_BAD) {
			Com_Error(ERR_DROP, "Shader '%s'with sort == SS_BAD", shader->name);
		}

		// if the mirror was completely clipped away, we may need to check another surface
		if (R_MirrorViewBySurface(drawSurfs + i, entityNum)) {
			// this is a debug option to see exactly what is being mirrored
			if (r_portalOnly->integer) {
				return;
			}
			break;		// only one mirror view at a time
		}
	}

	R_AddDrawSurfCmd(drawSurfs, numDrawSurfs);
}

/*
=============
R_AddEntitySurfaces
=============
*/
void R_AddEntitySurfaces() {
	shader_t* shader;

	if (!r_drawentities->integer) {
		return;
	}

	for (tr.currentEntityNum = 0;
		tr.currentEntityNum < tr.refdef.num_entities;
		tr.currentEntityNum++) {
		trRefEntity_t* ent = tr.currentEntity = &tr.refdef.entities[tr.currentEntityNum];

		ent->needDlights = qfalse;

		// preshift the value we are going to OR into the drawsurf sort
		tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

		if (ent->e.renderfx & RF_ALPHA_FADE)
		{
			// we need to make sure this is not sorted before the world..in fact we
			// want this to be sorted quite late...like how about last.
			// I don't want to use the highest bit, since no doubt someone fumbled
			// handling that as an unsigned quantity somewhere
			tr.shiftedEntityNum |= 0x80000000;
		}
		//
		// the weapon model must be handled special --
		// we don't want the hacked weapon position showing in
		// mirrors, because the true body position will already be drawn
		//
		if (ent->e.renderfx & RF_FIRST_PERSON && tr.viewParms.is_portal) {
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch (ent->e.reType) {
		case RT_PORTALSURFACE:
			break;		// don't draw anything
		case RT_SPRITE:
		case RT_ORIENTED_QUAD:
		case RT_BEAM:
		case RT_CYLINDER:
		case RT_LATHE:
		case RT_CLOUDS:
		case RT_LINE:
		case RT_ELECTRICITY:
		case RT_ORIENTEDLINE:
		case RT_SABER_GLOW:
		case RT_LIGHTNING:
			// self blood sprites, talk balloons, etc should not be drawn in the primary
			// view.  We can't just do this check for all entities, because md3
			// entities may still want to cast shadows from them
			if (ent->e.renderfx & RF_THIRD_PERSON && !tr.viewParms.is_portal) {
				continue;
			}
			shader = R_GetShaderByHandle(ent->e.customShader);
			R_AddDrawSurf(&entitySurface, shader, R_SpriteFogNum(ent), 0);
			break;

		case RT_MODEL:
			// we must set up parts of tr.or for model culling
			R_RotateForEntity(ent, &tr.viewParms, &tr.ori);

			tr.currentModel = R_GetModelByHandle(ent->e.hModel);
			if (!tr.currentModel) {
				R_AddDrawSurf(&entitySurface, tr.defaultShader, 0, 0);
			}
			else {
				switch (tr.currentModel->type) {
				case MOD_MESH:
					R_AddMD3Surfaces(ent);
					break;
				case MOD_BRUSH:
					R_AddBrushModelSurfaces(ent);
					break;
					/*
					Ghoul2 Insert Start
					*/

				case MOD_MDXM:
					R_AddGhoulSurfaces(ent);
					break;
				case MOD_BAD:		// null model axis
					if (ent->e.renderfx & RF_THIRD_PERSON && !tr.viewParms.is_portal)
					{
						if (!(ent->e.renderfx & RF_SHADOW_ONLY))
						{
							break;
						}
					}

					if (ent->e.ghoul2 && G2API_HaveWeGhoul2Models(*ent->e.ghoul2))
					{
						R_AddGhoulSurfaces(ent);
						break;
					}

					R_AddDrawSurf(&entitySurface, tr.defaultShader, 0, false);
					break;
					/*
					Ghoul2 Insert End
					*/

				default:
					Com_Error(ERR_DROP, "R_AddEntitySurfaces: Bad modeltype");
				}
			}
			break;
		default:
			Com_Error(ERR_DROP, "R_AddEntitySurfaces: Bad reType");
		}
	}
}

/*
====================
R_GenerateDrawSurfs
====================
*/
void R_GenerateDrawSurfs() {
	R_AddWorldSurfaces();

	R_AddPolygonSurfaces();

	// set the projection matrix with the minimum zfar
	// now that we have the world bounded
	// this needs to be done before entities are
	// added, because they use the projection
	// matrix for lod calculation
	R_SetupProjection();

	R_AddEntitySurfaces();
}

/*
================
R_DebugPolygon
================
*/
void R_DebugPolygon(const int color, const int num_points, const float* points) {
	int		i;

	GL_State(GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

	// draw solid shade

	qglColor3f(color & 1, color >> 1 & 1, color >> 2 & 1);
	qglBegin(GL_POLYGON);
	for (i = 0; i < num_points; i++) {
		qglVertex3fv(points + i * 3);
	}
	qglEnd();

	// draw wireframe outline
	GL_State(GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	qglDepthRange(0, 0);
	qglColor3f(1, 1, 1);
	qglBegin(GL_POLYGON);
	for (i = 0; i < num_points; i++) {
		qglVertex3fv(points + i * 3);
	}
	qglEnd();
	qglDepthRange(0, 1);
}

/*
====================
R_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
void R_DebugGraphics()
{
	if (!r_debugSurface->integer)
	{
		return;
	}

	// the render thread can't make callbacks to the main thread
	R_IssuePendingRenderCommands(); //

	GL_Bind(tr.whiteImage);
	GL_Cull(CT_FRONT_SIDED);
	ri.CM_DrawDebugSurface(R_DebugPolygon);
}

qboolean R_FogParmsMatch(const int fog1, const int fog2)
{
	for (int i = 0; i < 2; i++)
	{
		if (tr.world->fogs[fog1].parms.color[i] != tr.world->fogs[fog2].parms.color[i])
		{
			return qfalse;
		}
	}
	return qtrue;
}

void R_SetViewFogIndex()
{
	if (tr.world->numfogs > 1)
	{//more than just the LA goggles
		const int contents = ri.SV_PointContents(tr.refdef.vieworg, 0);
		if (contents & CONTENTS_FOG)
		{//only take a tr.refdef.fogIndex if the tr.refdef.vieworg is actually *in* that fog brush (assumption: checks pointcontents for any CONTENTS_FOG, not that particular brush...)
			for (tr.refdef.fogIndex = 1; tr.refdef.fogIndex < tr.world->numfogs; tr.refdef.fogIndex++)
			{
				const fog_t* fog = &tr.world->fogs[tr.refdef.fogIndex];
				if (tr.refdef.vieworg[0] >= fog->bounds[0][0]
					&& tr.refdef.vieworg[1] >= fog->bounds[0][1]
					&& tr.refdef.vieworg[2] >= fog->bounds[0][2]
					&& tr.refdef.vieworg[0] <= fog->bounds[1][0]
					&& tr.refdef.vieworg[1] <= fog->bounds[1][1]
					&& tr.refdef.vieworg[2] <= fog->bounds[1][2])
				{
					break;
				}
			}
			if (tr.refdef.fogIndex == tr.world->numfogs)
			{
				tr.refdef.fogIndex = 0;
			}
		}
		else
		{
			tr.refdef.fogIndex = 0;
		}
	}
	else
	{
		tr.refdef.fogIndex = 0;
	}
}
void RE_SetLightStyle(int style, int colors);

/*
================
R_RenderView

A view may be either the actual camera view,
or a mirror / remote location
================
*/
void R_RenderView(const viewParms_t* parms)
{
	if (parms->viewportWidth <= 0 || parms->viewportHeight <= 0) {
		return;
	}

	if (r_debugStyle->integer >= 0)
	{
		color4ub_t	whitecolor = { 0xff, 0xff, 0xff, 0xff };
		color4ub_t	blackcolor = { 0x00, 0x00, 0x00, 0xff };

		const byteAlias_t* ba = reinterpret_cast<byteAlias_t*>(&blackcolor);
		for (int i = 0; i < MAX_LIGHT_STYLES; i++) {
			RE_SetLightStyle(i, ba->i);
		}
		ba = reinterpret_cast<byteAlias_t*>(&whitecolor);
		RE_SetLightStyle(r_debugStyle->integer, ba->i);
	}

	tr.viewCount++;

	tr.viewParms = *parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;

	const int first_draw_surf = tr.refdef.numDrawSurfs;

	tr.viewCount++;

	// set viewParms.world
	R_RotateForViewer();

	R_SetupFrustum();

	if (!(tr.refdef.rdflags & RDF_NOWORLDMODEL))
	{	// Trying to do this with no world is not good.
		R_SetViewFogIndex();
	}

	R_GenerateDrawSurfs();

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
// wrapped around in the buffer and we will be missing
// the first surfaces, not the last ones
	int numDrawSurfs = tr.refdef.numDrawSurfs;
	if (numDrawSurfs > MAX_DRAWSURFS) {
		numDrawSurfs = MAX_DRAWSURFS;
	}

	R_SortDrawSurfs(tr.refdef.drawSurfs + first_draw_surf, numDrawSurfs - first_draw_surf);

	// draw main system development information (surface outlines, etc)
	R_DebugGraphics();
}