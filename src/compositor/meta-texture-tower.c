/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * MetaTextureTower
 *
 * Mipmap emulation by creation of scaled down images
 *
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "compositor/meta-texture-tower.h"

#ifndef M_LOG2E
#define M_LOG2E 1.4426950408889634074
#endif

#define MAX_TEXTURE_LEVELS 12

/* If the texture format in memory doesn't match this, then Mesa
 * will do the conversion, so things will still work, but it might
 * be slow depending on how efficient Mesa is. These should be the
 * native formats unless the display is 16bpp. If conversions
 * here are a bottleneck, investigate whether we are converting when
 * storing window data *into* the texture before adding extra code
 * to handle multiple texture formats.
 */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define TEXTURE_FORMAT COGL_PIXEL_FORMAT_BGRA_8888_PRE
#else
#define TEXTURE_FORMAT COGL_PIXEL_FORMAT_ARGB_8888_PRE
#endif

typedef struct
{
  guint16 x1;
  guint16 y1;
  guint16 x2;
  guint16 y2;
} Box;

struct _MetaTextureTower
{
  int n_levels;
  CoglTexture *textures[MAX_TEXTURE_LEVELS];
  CoglOffscreen *fbos[MAX_TEXTURE_LEVELS];
  Box invalid[MAX_TEXTURE_LEVELS];
  CoglPipeline *pipeline_template;
};

/**
 * meta_texture_tower_new:
 *
 * Creates a new texture tower. The base texture has to be set with
 * meta_texture_tower_set_base_texture() before use.
 *
 * Return value: the new texture tower. Free with meta_texture_tower_free()
 */
MetaTextureTower *
meta_texture_tower_new (void)
{
  MetaTextureTower *tower;

  tower = g_new0 (MetaTextureTower, 1);

  return tower;
}

/**
 * meta_texture_tower_free:
 * @tower: a #MetaTextureTower
 *
 * Frees a texture tower created with meta_texture_tower_new().
 */
void
meta_texture_tower_free (MetaTextureTower *tower)
{
  g_return_if_fail (tower != NULL);

  if (tower->pipeline_template != NULL)
    cogl_object_unref (tower->pipeline_template);

  meta_texture_tower_set_base_texture (tower, NULL);

  g_free (tower);
}

/**
 * meta_texture_tower_set_base_texture:
 * @tower: a #MetaTextureTower
 * @texture: the new texture used as a base for scaled down versions
 *
 * Sets the base texture that is the scaled texture that the
 * scaled textures of the tower are derived from. The texture itself
 * will be used as level 0 of the tower and will be referenced until
 * unset or until the tower is freed.
 */
void
meta_texture_tower_set_base_texture (MetaTextureTower *tower,
                                     CoglTexture      *texture)
{
  int i;

  g_return_if_fail (tower != NULL);

  if (texture == tower->textures[0])
    return;

  if (tower->textures[0] != NULL)
    {
      for (i = 1; i < tower->n_levels; i++)
        {
          cogl_clear_object (&tower->textures[i]);
          g_clear_object (&tower->fbos[i]);
        }

      cogl_object_unref (tower->textures[0]);
    }

  tower->textures[0] = texture;

  if (tower->textures[0] != NULL)
    {
      int width, height;

      cogl_object_ref (tower->textures[0]);

      width = cogl_texture_get_width (tower->textures[0]);
      height = cogl_texture_get_height (tower->textures[0]);

      tower->n_levels = 1 + MAX ((int)(M_LOG2E * log (width)), (int)(M_LOG2E * log (height)));
      tower->n_levels = MIN (tower->n_levels, MAX_TEXTURE_LEVELS);

      meta_texture_tower_update_area (tower, 0, 0, width, height);
    }
  else
    {
      tower->n_levels = 0;
    }
}

/**
 * meta_texture_tower_update_area:
 * @tower: a #MetaTextureTower
 * @x: X coordinate of upper left of rectangle that changed
 * @y: Y coordinate of upper left of rectangle that changed
 * @width: width of rectangle that changed
 * @height: height rectangle that changed
 *
 * Mark a region of the base texture as having changed; the next
 * time a scaled down version of the base texture is retrieved,
 * the appropriate area of the scaled down texture will be updated.
 */
void
meta_texture_tower_update_area (MetaTextureTower *tower,
                                int               x,
                                int               y,
                                int               width,
                                int               height)
{
  int texture_width, texture_height;
  Box invalid;
  int i;

  g_return_if_fail (tower != NULL);

  if (tower->textures[0] == NULL)
    return;

  texture_width = cogl_texture_get_width (tower->textures[0]);
  texture_height = cogl_texture_get_height (tower->textures[0]);

  invalid.x1 = x;
  invalid.y1 = y;
  invalid.x2 = x + width;
  invalid.y2 = y + height;

  for (i = 1; i < tower->n_levels; i++)
    {
      texture_width = MAX (1, texture_width / 2);
      texture_height = MAX (1, texture_height / 2);

      invalid.x1 = invalid.x1 / 2;
      invalid.y1 = invalid.y1 / 2;
      invalid.x2 = MIN (texture_width, (invalid.x2 + 1) / 2);
      invalid.y2 = MIN (texture_height, (invalid.y2 + 1) / 2);

      if (tower->invalid[i].x1 == tower->invalid[i].x2 ||
          tower->invalid[i].y1 == tower->invalid[i].y2)
        {
          tower->invalid[i] = invalid;
        }
      else
        {
          tower->invalid[i].x1 = MIN (tower->invalid[i].x1, invalid.x1);
          tower->invalid[i].y1 = MIN (tower->invalid[i].y1, invalid.y1);
          tower->invalid[i].x2 = MAX (tower->invalid[i].x2, invalid.x2);
          tower->invalid[i].y2 = MAX (tower->invalid[i].y2, invalid.y2);
        }
    }
}

/* It generally looks worse if we scale up a window texture by even a
 * small amount than if we scale it down using bilinear filtering, so
 * we always pick the *larger* adjacent level. */
#define LOD_BIAS (-0.49)

/* This determines the appropriate level of detail to use when drawing the
 * texture, in a way that corresponds to what the GL specification does
 * when mip-mapping. This is probably fancier and slower than what we need,
 * but we do the computation only once each time we paint a window, and
 * its easier to just use the equations from the specification than to
 * come up with something simpler.
 *
 * If window is being painted at an angle from the viewer, then we have to
 * pick a point in the texture; we use the middle of the texture (which is
 * why the width/height are passed in.) This is not the normal case for
 * Meta.
 */
static int
get_paint_level (ClutterPaintContext *paint_context,
                 int                  width,
                 int                  height)
{
  CoglFramebuffer *framebuffer;
  graphene_matrix_t projection, modelview, pm;
  float xx, xy, xw;
  float yx, yy, yw;
  float wx, wy, ww;
  float v[4];
  double viewport_width, viewport_height;
  double u0, v0;
  double xc, yc, wc;
  double dxdu_, dxdv_, dydu_, dydv_;
  double det_, det_sq;
  double rho_sq;
  double lambda;

  /* See
   * http://www.opengl.org/registry/doc/glspec32.core.20090803.pdf
   * Section 3.8.9, p. 1.6.2. Here we have
   *
   *  u(x,y) = x_o;
   *  v(x,y) = y_o;
   *
   * Since we are mapping 1:1 from object coordinates into pixel
   * texture coordinates, the clip coordinates are:
   *
   *  (x_c)                               (x_o)        (u)
   *  (y_c) = (M_projection)(M_modelview) (y_o) = (PM) (v)
   *  (z_c)                               (z_o)        (0)
   *  (w_c)                               (w_o)        (1)
   */

  framebuffer = clutter_paint_context_get_framebuffer (paint_context);
  cogl_framebuffer_get_projection_matrix (framebuffer, &projection);
  cogl_framebuffer_get_modelview_matrix (framebuffer, &modelview);

  graphene_matrix_multiply (&modelview, &projection, &pm);

  xx = graphene_matrix_get_value (&pm, 0, 0);
  xy = graphene_matrix_get_value (&pm, 0, 1);
  xw = graphene_matrix_get_value (&pm, 0, 3);
  yx = graphene_matrix_get_value (&pm, 1, 0);
  yy = graphene_matrix_get_value (&pm, 1, 1);
  yw = graphene_matrix_get_value (&pm, 1, 3);
  wx = graphene_matrix_get_value (&pm, 3, 0);
  wy = graphene_matrix_get_value (&pm, 3, 1);
  ww = graphene_matrix_get_value (&pm, 3, 3);

  cogl_framebuffer_get_viewport4fv (framebuffer, v);
  viewport_width = v[2];
  viewport_height = v[3];

  u0 = width / 2.;
  v0 = height / 2.;

  xc = xx * u0 + yx * v0 + wx;
  yc = xy * u0 + yy * v0 + wy;
  wc = xw * u0 + yw * v0 + ww;

  /* We'll simplify the equations below for a bit of micro-optimization.
   * The commented out code is the unsimplified version.

  // Partial derivates of window coordinates:
  //
  //  x_w = 0.5 * viewport_width * x_c / w_c + viewport_center_x
  //  y_w = 0.5 * viewport_height * y_c / w_c + viewport_center_y
  //
  // with respect to u, v, using
  // d(a/b)/dx = da/dx * (1/b) - a * db/dx / (b^2)

  dxdu = 0.5 * viewport_width * (xx - xw * (xc/wc)) / wc;
  dxdv = 0.5 * viewport_width * (yx - yw * (xc/wc)) / wc;
  dydu = 0.5 * viewport_height * (xy - xw * (yc/wc)) / wc;
  dydv = 0.5 * viewport_height * (yy - yw * (yc/wc)) / wc;

  // Compute the inverse partials as the matrix inverse
  det = dxdu * dydv - dxdv * dydu;

  dudx =   dydv / det;
  dudy = - dxdv / det;
  dvdx = - dydu / det;
  dvdy =   dvdu / det;

  // Scale factor; maximum of the distance in texels for a change of 1 pixel
  // in the X direction or 1 pixel in the Y direction
  rho = MAX (sqrt (dudx * dudx + dvdx * dvdx), sqrt(dudy * dudy + dvdy * dvdy));

  // Level of detail
  lambda = log2 (rho) + LOD_BIAS;
  */

  /* dxdu * wc, etc */
  dxdu_ = 0.5 * viewport_width * (xx - xw * (xc/wc));
  dxdv_ = 0.5 * viewport_width * (yx - yw * (xc/wc));
  dydu_ = 0.5 * viewport_height * (xy - xw * (yc/wc));
  dydv_ = 0.5 * viewport_height * (yy - yw * (yc/wc));

  /* det * wc^2 */
  det_ = dxdu_ * dydv_ - dxdv_ * dydu_;
  det_sq = det_ * det_;
  if (det_sq == 0.0)
    return -1;

  /* (rho * det * wc)^2 */
  rho_sq = MAX (dydv_ * dydv_ + dydu_ * dydu_, dxdv_ * dxdv_ + dxdu_ * dxdu_);
  lambda = 0.5 * M_LOG2E * log (rho_sq * wc * wc / det_sq) + LOD_BIAS;

#if 0
  g_print ("%g %g %g\n", 0.5 * viewport_width * xx / ww, 0.5 * viewport_height * yy / ww, lambda);
#endif

  if (lambda <= 0.)
    return 0;
  else
    return (int)(0.5 + lambda);
}

static void
texture_tower_create_texture (MetaTextureTower *tower,
                              int               level,
                              int               width,
                              int               height)
{
  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());

  tower->textures[level] =
    COGL_TEXTURE (cogl_texture_2d_new_with_size (ctx, width, height));

  tower->invalid[level].x1 = 0;
  tower->invalid[level].y1 = 0;
  tower->invalid[level].x2 = width;
  tower->invalid[level].y2 = height;
}

static void
texture_tower_revalidate (MetaTextureTower *tower,
                          int               level)
{
  CoglTexture *source_texture = tower->textures[level - 1];
  int source_texture_width = cogl_texture_get_width (source_texture);
  int source_texture_height = cogl_texture_get_height (source_texture);
  CoglTexture *dest_texture = tower->textures[level];
  int dest_texture_width = cogl_texture_get_width (dest_texture);
  int dest_texture_height = cogl_texture_get_height (dest_texture);
  Box *invalid = &tower->invalid[level];
  CoglFramebuffer *fb;
  GError *catch_error = NULL;
  CoglPipeline *pipeline;

  if (tower->fbos[level] == NULL)
    tower->fbos[level] = cogl_offscreen_new_with_texture (dest_texture);

  fb = COGL_FRAMEBUFFER (tower->fbos[level]);

  if (!cogl_framebuffer_allocate (fb, &catch_error))
    {
      g_error_free (catch_error);
      return;
    }

  cogl_framebuffer_orthographic (fb, 0, 0, dest_texture_width, dest_texture_height, -1., 1.);

  if (!tower->pipeline_template)
    {
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());
      tower->pipeline_template = cogl_pipeline_new (ctx);
      cogl_pipeline_set_blend (tower->pipeline_template, "RGBA = ADD (SRC_COLOR, 0)", NULL);
    }

  pipeline = cogl_pipeline_copy (tower->pipeline_template);
  cogl_pipeline_set_layer_texture (pipeline, 0, tower->textures[level - 1]);

  cogl_framebuffer_draw_textured_rectangle (fb, pipeline,
                                            invalid->x1, invalid->y1,
                                            invalid->x2, invalid->y2,
                                            (2. * invalid->x1) / source_texture_width,
                                            (2. * invalid->y1) / source_texture_height,
                                            (2. * invalid->x2) / source_texture_width,
                                            (2. * invalid->y2) / source_texture_height);

  cogl_object_unref (pipeline);

  tower->invalid[level].x1 = tower->invalid[level].x2 = 0;
  tower->invalid[level].y1 = tower->invalid[level].y2 = 0;
}

/**
 * meta_texture_tower_get_paint_texture:
 * @tower: a #MetaTextureTower
 * @paint_context: a #ClutterPaintContext
 *
 * Gets the texture from the tower that best matches the current
 * rendering scale. (On the assumption here the texture is going to
 * be rendered with vertex coordinates that correspond to its
 * size in pixels, so a 200x200 texture will be rendered on the
 * rectangle (0, 0, 200, 200).
 *
 * Return value: the COGL texture handle to use for painting, or
 *  %NULL if no base texture has yet been set.
 */
CoglTexture *
meta_texture_tower_get_paint_texture (MetaTextureTower    *tower,
                                      ClutterPaintContext *paint_context)
{
  int texture_width, texture_height;
  int level;

  g_return_val_if_fail (tower != NULL, NULL);

  if (tower->textures[0] == NULL)
    return NULL;

  texture_width = cogl_texture_get_width (tower->textures[0]);
  texture_height = cogl_texture_get_height (tower->textures[0]);

  level = get_paint_level (paint_context, texture_width, texture_height);
  if (level < 0) /* singular paint matrix, scaled to nothing */
    return NULL;
  level = MIN (level, tower->n_levels - 1);

  if (tower->textures[level] == NULL ||
      (tower->invalid[level].x2 != tower->invalid[level].x1 &&
       tower->invalid[level].y2 != tower->invalid[level].y1))
    {
      int i;

      for (i = 1; i <= level; i++)
        {
          /* Use "floor" convention here to be consistent with the NPOT texture extension */
          texture_width = MAX (1, texture_width / 2);
          texture_height = MAX (1, texture_height / 2);

          if (tower->textures[i] == NULL)
            texture_tower_create_texture (tower, i, texture_width, texture_height);
        }

      for (i = 1; i <= level; i++)
        {
          if (tower->invalid[level].x2 != tower->invalid[level].x1 &&
              tower->invalid[level].y2 != tower->invalid[level].y1)
            texture_tower_revalidate (tower, i);
        }
    }

  return tower->textures[level];
}
