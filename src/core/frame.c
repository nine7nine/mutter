/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Mutter X window decorations */

/*
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003, 2004 Red Hat, Inc.
 * Copyright (C) 2005 Elijah Newren
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

#include "core/frame.h"

#include "backends/x11/meta-backend-x11.h"
#include "core/bell.h"
#include "core/keybindings-private.h"
#include "meta/meta-x11-errors.h"
#include "x11/meta-x11-display-private.h"

#include <X11/Xatom.h>

#define EVENT_MASK (SubstructureRedirectMask |                     \
                    StructureNotifyMask | SubstructureNotifyMask | \
                    ExposureMask | FocusChangeMask)

static void
send_frame_request (MetaWindow *window)
{
  MetaX11Display *x11_display = window->display->x11_display;
  Display *xdisplay = x11_display->xdisplay;
  XEvent xev = { 0 };

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = XInternAtom (xdisplay, "_MUTTER_FRAME", False);
  xev.xclient.format = 32;
  xev.xclient.window = x11_display->xroot;
  xev.xclient.data.l[0] = window->xwindow;

  meta_x11_error_trap_push (x11_display);
  XSendEvent (xdisplay, x11_display->xroot, False, ClientMessage, &xev);
  meta_x11_error_trap_pop (x11_display);
}

void
meta_window_ensure_frame (MetaWindow *window)
{
  MetaFrame *frame;

  if (window->frame)
    return;

  frame = g_new0 (MetaFrame, 1);

  frame->window = window;
  frame->xwindow = None;

  frame->rect = window->rect;
  frame->child_x = 0;
  frame->child_y = 0;
  frame->bottom_height = 0;
  frame->right_width = 0;
  frame->current_cursor = 0;

  frame->borders_cached = FALSE;

  send_frame_request (window);

  window->frame = frame;

  meta_verbose ("Frame geometry %d,%d  %dx%d",
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);
}

void
meta_window_set_frame_xwindow (MetaWindow *window,
                               Window      xframe)
{
  MetaX11Display *x11_display = window->display->x11_display;
  MetaFrame *frame = window->frame;
  gulong create_serial = 0;

  meta_verbose ("Setting frame 0x%lx for window %s, "
                "frame geometry %d,%d  %dx%d",
                xframe, window->desc,
                frame->rect.x, frame->rect.y,
                frame->rect.width, frame->rect.height);

  frame->xwindow = xframe;

  meta_stack_tracker_record_add (window->display->stack_tracker,
                                 frame->xwindow,
                                 create_serial);

  meta_verbose ("Frame for %s is 0x%lx", frame->window->desc, frame->xwindow);

  meta_x11_display_register_x_window (x11_display, &frame->xwindow, window);

  meta_x11_error_trap_push (x11_display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* the reparent will unmap the window,
                               * we don't want to take that as a withdraw
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent", window->desc);
      window->unmaps_pending += 1;
    }

  meta_stack_tracker_record_remove (window->display->stack_tracker,
                                    window->xwindow,
                                    XNextRequest (x11_display->xdisplay));
  XReparentWindow (x11_display->xdisplay,
                   window->xwindow,
                   frame->xwindow,
                   frame->child_x,
                   frame->child_y);
  window->reparents_pending += 1;
  /* FIXME handle this error */
  meta_x11_error_trap_pop (x11_display);

  XSelectInput (x11_display->xdisplay,
                frame->xwindow,
                KeyPressMask | PropertyChangeMask);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  /* stick frame to the window */
  window->frame = frame;

  /* Move keybindings to frame instead of window */
  meta_window_grab_keys (window);
}

void
meta_window_destroy_frame (MetaWindow *window)
{
  MetaFrame *frame;
  MetaFrameBorders borders;
  MetaX11Display *x11_display;

  if (window->frame == NULL)
    return;

  x11_display = window->display->x11_display;

  meta_verbose ("Unframing window %s", window->desc);

  frame = window->frame;

  meta_frame_calc_borders (frame, &borders);

  /* Unparent the client window; it may be destroyed,
   * thus the error trap.
   */
  meta_x11_error_trap_push (x11_display);
  if (window->mapped)
    {
      window->mapped = FALSE; /* Keep track of unmapping it, so we
                               * can identify a withdraw initiated
                               * by the client.
                               */
      meta_topic (META_DEBUG_WINDOW_STATE,
                  "Incrementing unmaps_pending on %s for reparent back to root", window->desc);
      window->unmaps_pending += 1;
    }

  if (!x11_display->closing)
    {
      meta_stack_tracker_record_add (window->display->stack_tracker,
                                     window->xwindow,
                                     XNextRequest (x11_display->xdisplay));

      XReparentWindow (x11_display->xdisplay,
                       window->xwindow,
                       x11_display->xroot,
                       /* Using anything other than client root window coordinates
                        * coordinates here means we'll need to ensure a configure
                        * notify event is sent; see bug 399552.
                        */
                       window->frame->rect.x + borders.invisible.left,
                       window->frame->rect.y + borders.invisible.top);
      window->reparents_pending += 1;
    }

  meta_x11_error_trap_pop (x11_display);

  /* Ensure focus is restored after the unmap/map events triggered
   * by XReparentWindow().
   */
  if (meta_window_has_focus (window))
    window->restore_focus_on_map = TRUE;

  meta_x11_display_unregister_x_window (x11_display, frame->xwindow);

  window->frame = NULL;
  if (window->frame_bounds)
    {
      cairo_region_destroy (window->frame_bounds);
      window->frame_bounds = NULL;
    }

  /* Move keybindings to window instead of frame */
  meta_window_grab_keys (window);

  g_free (frame);

  /* Put our state back where it should be */
  meta_window_queue (window, META_QUEUE_CALC_SHOWING);
  meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
}


MetaFrameFlags
meta_frame_get_flags (MetaFrame *frame)
{
  MetaFrameFlags flags;

  flags = 0;

  if (frame->window->border_only)
    {
      ; /* FIXME this may disable the _function_ as well as decor
         * in some cases, which is sort of wrong.
         */
    }
  else
    {
      flags |= META_FRAME_ALLOWS_MENU;

      if (frame->window->has_close_func)
        flags |= META_FRAME_ALLOWS_DELETE;

      if (frame->window->has_maximize_func)
        flags |= META_FRAME_ALLOWS_MAXIMIZE;

      if (frame->window->has_minimize_func)
        flags |= META_FRAME_ALLOWS_MINIMIZE;

      if (frame->window->has_shade_func)
        flags |= META_FRAME_ALLOWS_SHADE;
    }

  if (META_WINDOW_ALLOWS_MOVE (frame->window))
    flags |= META_FRAME_ALLOWS_MOVE;

  if (META_WINDOW_ALLOWS_HORIZONTAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_HORIZONTAL_RESIZE;

  if (META_WINDOW_ALLOWS_VERTICAL_RESIZE (frame->window))
    flags |= META_FRAME_ALLOWS_VERTICAL_RESIZE;

  if (meta_window_appears_focused (frame->window))
    flags |= META_FRAME_HAS_FOCUS;

  if (frame->window->shaded)
    flags |= META_FRAME_SHADED;

  if (frame->window->on_all_workspaces_requested)
    flags |= META_FRAME_STUCK;

  /* FIXME: Should we have some kind of UI for windows that are just vertically
   * maximized or just horizontally maximized?
   */
  if (META_WINDOW_MAXIMIZED (frame->window))
    flags |= META_FRAME_MAXIMIZED;

  if (META_WINDOW_TILED_LEFT (frame->window))
    flags |= META_FRAME_TILED_LEFT;

  if (META_WINDOW_TILED_RIGHT (frame->window))
    flags |= META_FRAME_TILED_RIGHT;

  if (frame->window->fullscreen)
    flags |= META_FRAME_FULLSCREEN;

  if (frame->window->wm_state_above)
    flags |= META_FRAME_ABOVE;

  return flags;
}

void
meta_frame_borders_clear (MetaFrameBorders *self)
{
  self->visible.top    = self->invisible.top    = self->total.top    = 0;
  self->visible.bottom = self->invisible.bottom = self->total.bottom = 0;
  self->visible.left   = self->invisible.left   = self->total.left   = 0;
  self->visible.right  = self->invisible.right  = self->total.right  = 0;
}

static void
meta_frame_query_borders (MetaFrame        *frame,
                          MetaFrameBorders *borders)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;
  int format, res;
  Atom type;
  unsigned long nitems, bytes_after;
  unsigned char *data;

  if (!frame->xwindow)
    return;

  meta_x11_error_trap_push (x11_display);

  res = XGetWindowProperty (x11_display->xdisplay,
                            frame->xwindow,
                            x11_display->atom__GTK_FRAME_EXTENTS,
                            0, 4,
                            False, XA_CARDINAL,
                            &type, &format,
                            &nitems, &bytes_after,
                            (unsigned char **) &data);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    return;

  if (res == Success && nitems == 4)
    {
      borders->invisible = (GtkBorder) {
        ((long *) data)[0],
        ((long *) data)[1],
        ((long *) data)[2],
        ((long *) data)[3],
      };
    }

  g_clear_pointer (&data, XFree);

  meta_x11_error_trap_push (x11_display);

  res = XGetWindowProperty (x11_display->xdisplay,
                            frame->xwindow,
                            x11_display->atom__MUTTER_FRAME_HEIGHT,
                            0, 1,
                            False, XA_CARDINAL,
                            &type, &format,
                            &nitems, &bytes_after,
                            (unsigned char **) &data);

  if (meta_x11_error_trap_pop_with_return (x11_display) != Success)
    return;

  if (res == Success && nitems == 1)
    {
      borders->visible = (GtkBorder) {
        0, 0, ((long *) data)[0], 0,
      };
    }

  g_clear_pointer (&data, XFree);

  borders->total = (GtkBorder) {
    borders->invisible.left + frame->cached_borders.visible.left,
    borders->invisible.right + frame->cached_borders.visible.right,
    borders->invisible.top + frame->cached_borders.visible.top,
    borders->invisible.bottom + frame->cached_borders.visible.bottom,
  };
}

void
meta_frame_calc_borders (MetaFrame        *frame,
                         MetaFrameBorders *borders)
{
  /* Save on if statements and potential uninitialized values
   * in callers -- if there's no frame, then zero the borders. */
  if (frame == NULL)
    meta_frame_borders_clear (borders);
  else
    {
      if (!frame->borders_cached)
        {
          meta_frame_query_borders (frame, &frame->cached_borders);
          frame->borders_cached = TRUE;
        }

      *borders = frame->cached_borders;
    }
}

void
meta_frame_clear_cached_borders (MetaFrame *frame)
{
  frame->borders_cached = FALSE;
}

gboolean
meta_frame_sync_to_window (MetaFrame *frame,
                           gboolean   need_resize)
{
  meta_topic (META_DEBUG_GEOMETRY,
              "Syncing frame geometry %d,%d %dx%d (SE: %d,%d)",
              frame->rect.x, frame->rect.y,
              frame->rect.width, frame->rect.height,
              frame->rect.x + frame->rect.width,
              frame->rect.y + frame->rect.height);

  return need_resize;
}

cairo_region_t *
meta_frame_get_frame_bounds (MetaFrame *frame)
{
  return NULL;
}

void
meta_frame_get_mask (MetaFrame             *frame,
                     cairo_rectangle_int_t *frame_rect,
                     cairo_t               *cr)
{
  MetaFrameBorders borders;

  meta_frame_query_borders (frame, &borders);

  cairo_rectangle (cr,
                   borders.invisible.left,
                   borders.invisible.top,
                   frame_rect->width,
                   frame_rect->height);
  cairo_set_source_rgb (cr, 0, 0, 0);
  cairo_fill (cr);
}

Window
meta_frame_get_xwindow (MetaFrame *frame)
{
  return frame->xwindow;
}

gboolean
meta_frame_handle_xevent (MetaFrame *frame,
                          XEvent    *xevent)
{
  MetaWindow *window = frame->window;
  MetaX11Display *x11_display = window->display->x11_display;

  if (xevent->xany.type == PropertyNotify &&
      xevent->xproperty.state == PropertyNewValue &&
      (xevent->xproperty.atom == x11_display->atom__GTK_FRAME_EXTENTS ||
       xevent->xproperty.atom == x11_display->atom__MUTTER_FRAME_HEIGHT))
    {
      meta_frame_query_borders (frame, &frame->cached_borders);
      meta_window_queue (window, META_QUEUE_MOVE_RESIZE);
      frame->borders_cached = TRUE;
      return TRUE;
    }

  return FALSE;
}

/**
 * meta_frame_type_to_string:
 * @type: a #MetaFrameType
 *
 * Converts a frame type enum value to the name string that would
 * appear in the theme definition file.
 *
 * Return value: the string value
 */
const char*
meta_frame_type_to_string (MetaFrameType type)
{
  switch (type)
    {
    case META_FRAME_TYPE_NORMAL:
      return "normal";
    case META_FRAME_TYPE_DIALOG:
      return "dialog";
    case META_FRAME_TYPE_MODAL_DIALOG:
      return "modal_dialog";
    case META_FRAME_TYPE_UTILITY:
      return "utility";
    case META_FRAME_TYPE_MENU:
      return "menu";
    case META_FRAME_TYPE_BORDER:
      return "border";
    case META_FRAME_TYPE_ATTACHED:
      return "attached";
#if 0
    case META_FRAME_TYPE_TOOLBAR:
      return "toolbar";
#endif
    case  META_FRAME_TYPE_LAST:
      break;
    }

  return "<unknown>";
}

static void
on_frames_died (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  GSubprocess *proc = user_data;
  g_autoptr (GError) error = NULL;

  if (!g_subprocess_wait_finish (proc, result, &error))
    g_warning ("Mutter X11 frames client died: %s\n", error->message);
}

static void
on_x11_display_setup (MetaDisplay *display,
                      gpointer     user_data)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  GSubprocess *proc;
  const char *args[2];

  args[0] = MUTTER_LIBEXECDIR "/mutter-x11-frames";
  args[1] = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher, "DISPLAY",
                                meta_x11_get_display_name (), TRUE);

  proc = g_subprocess_launcher_spawnv (launcher, args, &error);
  if (proc)
    g_subprocess_wait_async (proc, NULL, on_frames_died, NULL);

  if (error)
    g_warning ("Could not launch X11 frames client: %s", error->message);
}

static void
on_x11_display_closing (MetaDisplay *display,
                        gpointer     user_data)
{
}

void
meta_frame_initialize (MetaDisplay *display)
{
  g_signal_connect (display, "x11-display-setup",
                    G_CALLBACK (on_x11_display_setup),
                    NULL);
  g_signal_connect (display, "x11-display-closing",
                    G_CALLBACK (on_x11_display_closing),
                    NULL);
}
