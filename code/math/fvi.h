/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 



#ifndef _FVI_H
#define _FVI_H

#include "globalincs/pstypes.h"

//finds distance from point to plane
float fvi_point_dist_plane(	vec3d *plane_pnt,vec3d *plane_norm, vec3d *point);

// fvi functions - fvi stands for Find Vector Intersection
// fvi_a_b - means find the intersection of something of type a with something of type b
// type can be:
// ray:  a line from p0 through p1 extending to inifinity
// segment: a line from p0 stopping at p1
// sphere, point, face

//--------------------------------------------------------------------
// fvi_ray_plane - Finds the point on the specified plane where the 
// infinite ray intersects.
//
// Returns scaled-distance plane is from the ray_origin (t), so
// P = O + t*D, where P is the point of intersection, O is the ray origin,
// and D is the ray's direction.  So 0.0 would mean the intersection is 
// exactly on the ray origin, 1.0 would be on the ray origin plus the ray
// direction vector, anything negative would be behind the ray's origin.
// If you pass a pointer to the new_pnt, this routine will perform the P=
// calculation to calculate the point of intersection and put the result
// in *new_pnt.
//
// If radius is anything other than 0.0, it assumes you want the intersection
// point to be that radius from the plane.
//
// Note that ray_direction doesn't have to be normalized unless you want
// the return value to be in units from the ray origin.
//
// Also note that new_pnt will always be filled in to some valid value,
// even if it is a point at infinity.
//
// If the plane and line are parallel, this will return the largest 
// negative float number possible.
//
// So if you want to check a line segment from p0 to p1, you would pass
// p0 as ray_origin, p1-p0 as the ray_direction, and there would be an
// intersection if the return value is between 0 and 1.

float fvi_ray_plane(vec3d *new_pnt,
                    vec3d *plane_pnt,vec3d *plane_norm,		// Plane description, a point and a normal
                    vec3d *ray_origin,vec3d *ray_direction,	// Ray description, a point and a direction
						  float rad);


//find the point on the specified plane where the line segment intersects
//returns true if point found, false if line parallel to plane
//new_pnt is the found point on the plane
//plane_pnt & plane_norm describe the plane
//p0 & p1 are the ends of the line
int fvi_segment_plane(vec3d *new_pnt, vec3d *plane_pnt, vec3d *plane_norm, vec3d *p0, vec3d *p1, float rad);


// fvi_point_face
// see if a point in inside a face by projecting into 2d. Also
// Finds uv's if uvls is not NULL.  Returns 0 if point isn't on
// face, non-zero otherwise.
// From Graphics Gems I, "An efficient Ray-Polygon intersection", p390
// checkp - The point to check
// nv - how many verts in the poly
// verts - the vertives for the polygon 
// norm1 - the polygon's normal
// u_out,vout - if not null and v_out not null and uvls not_null and point is on face, the uv's of where it hit
// uvls - a list of uv pairs for each vertex
// This replaces the old check_point_to_face & find_hitpoint_uv
int fvi_point_face(vec3d *checkp, int nv, vec3d **verts, vec3d * norm1, float *u_out, float *v_out, uv_pair * uvls );


//maybe this routine should just return the distance and let the caller
//decide it it's close enough to hit
//determine if and where a vector intersects with a sphere
//vector defined by p0,p1 
//returns 1 if intersects, and fills in intp
//else returns 0
int fvi_segment_sphere(vec3d *intp, vec3d *p0, vec3d *p1, vec3d *sphere_pos, float sphere_rad);

//determine if and where a ray intersects with a sphere
//vector defined by p0,p1 
//returns 1 if intersects, and fills in intp
//else returns 0
int fvi_ray_sphere(vec3d *intp, vec3d *p0, vec3d *p1, vec3d *sphere_pos, float sphere_rad);


//==============================================================
// Finds intersection of a ray and an axis-aligned bounding box
// Given a ray with origin at p0, and direction pdir, this function
// returns non-zero if that ray intersects an axis-aligned bounding box
// from min to max.   If there was an intersection, then hitpt will contain
// the point where the ray begins inside the box.
// Fast ray-box intersection taken from Graphics Gems I, pages 395,736.
//int fvi_ray_boundingbox( vec3d *min, vec3d *max, vec3d * p0, vec3d *pdir, vec3d *hitpt );

/**
 * Finds intersection of a ray and an axis-aligned bounding box
 *
 * Given a ray with origin at p0, and direction pdir, this function
 * returns non-zero if that ray intersects an axis-aligned bounding box
 * from min to max.   If there was an intersection, then hitpt will contain
 * the point where the ray begins inside the box.
 * Fast ray-box intersection taken from Graphics Gems I, pages 395,736.
 */
inline int fvi_ray_boundingbox( vec3d *min, vec3d *max, vec3d * p0, vec3d *pdir, vec3d *hitpt )
{
  bool inside = true;
  bool middle[3] = { true, true, true };
  int i;
  int which_plane;
  float maxt[3];
  float candidate_plane[3];

  for (i = 0; i < 3; i++) {
    if (p0->a1d[i] < min->a1d[i]) {
      candidate_plane[i] = min->a1d[i];
      middle[i] = false;
      inside = false;
    } else if (p0->a1d[i] > max->a1d[i]) {
      candidate_plane[i] = max->a1d[i];
      middle[i] = false;
      inside = false;
    }
  }

  // ray origin inside bounding box
  if ( inside ) {
    *hitpt = *p0;
    return 1;
  }

  // calculate T distances to candidate plane
  for (i = 0; i < 3; i++) {
    if ( !middle[i] && (pdir->a1d[i] != 0.0f) )
      maxt[i] = (candidate_plane[i] - p0->a1d[i]) / pdir->a1d[i];
    else
      maxt[i] = -1.0f;
  }

  // Get largest of the maxt's for final choice of intersection
  which_plane = 0;
  for (i = 1; i < 3; i++) {
    if (maxt[which_plane] < maxt[i])
      which_plane = i;
  }

  // check final candidate actually inside box
  if (maxt[which_plane] < 0.0f)
    return 0;

  for (i = 0; i < 3; i++) {
    if (which_plane != i) {
      hitpt->a1d[i] = p0->a1d[i] + maxt[which_plane] * pdir->a1d[i];

      if ( (hitpt->a1d[i] < min->a1d[i]) || (hitpt->a1d[i] > max->a1d[i]) )
        return 0;
    } else {
      hitpt->a1d[i] = candidate_plane[i];
    }
  }

  return 1;
}

// sphere polygon collision prototypes

// Given a polygon vertex list and a moving sphere, find the first contact the sphere makes with the edge, if any
int fvi_polyedge_sphereline(vec3d *hit_point, vec3d *xs0, vec3d *vs, float Rs, int nv, vec3d **verts, float *hit_time);

int fvi_sphere_plane(vec3d *intersect_point, vec3d *sphere_center_start, vec3d *sphere_velocity, float sphere_radius, 
							vec3d *plane_normal, vec3d *plane_point, float *hit_time, float *delta_time);

// finds the point of intersection between two lines or the closest points if lines do not intersect
// closest points - line 1:  p1 + v1 * s,  line 2:  p2 + v2 * t
// p1 - point on line 1
// v1 - vector direction of line 1
// p2 - point on line 2
// v2 - vector direction of line 2
// s - parameter of intersection of line 1
// t - parameter of intersection of line 2
void fvi_two_lines_in_3space(vec3d *p1, vec3d *v1, vec3d *p2, vec3d *v2, float *s, float *t);

// vec3d mins - minimum extents of bbox
// vec3d maxs - maximum extents of bbox
// vec3d start - point in bbox reference frame
// vec3d box_pt - point in bbox reference frame.
// NOTE: if a coordinate of start is *inside* the bbox, it is *not* moved to surface of bbox
// return: 1 if inside, 0 otherwise.
int project_point_onto_bbox(vec3d *mins, vec3d *maxs, vec3d *start, vec3d *box_pt);

#endif
