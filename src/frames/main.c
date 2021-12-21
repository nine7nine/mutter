#include <gdk/x11/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

static GHashTable *frames = NULL;
static GHashTable *client_windows = NULL;

typedef struct _MetaFrameContent
{
  GtkWidget parent_instance;
  Window content;
  cairo_rectangle_int_t rect;
  int width, height;
  int frame_height;
} MetaFrameContent;

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} MotifWmHints;

#define MWM_FUNC_MINIMIZE (1L << 3)
#define MWM_FUNC_MAXIMIZE (1L << 4)
#define MWM_FUNC_CLOSE    (1L << 5)

G_DECLARE_FINAL_TYPE (MetaFrameContent, meta_frame_content,
                      META, FRAME_CONTENT, GtkWidget)

G_DEFINE_TYPE (MetaFrameContent, meta_frame_content, GTK_TYPE_WIDGET)

#define META_TYPE_FRAME_CONTENT (meta_frame_content_get_type ())

static void
meta_frame_content_update_frame_height (MetaFrameContent *content,
                                        int               height)
{
  GdkDisplay *display;
  GtkWindow *window;
  GdkSurface *surface;
  Window xframe;

  if (content->frame_height == height)
    return;

  content->frame_height = height;

  display = gtk_widget_get_display (GTK_WIDGET (content));
  window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (content)));
  surface = gtk_native_get_surface (GTK_NATIVE (window));
  xframe = gdk_x11_surface_get_xid (surface);

  XChangeProperty (gdk_x11_display_get_xdisplay (display),
                   xframe,
                   gdk_x11_get_xatom_by_name_for_display (display, "_MUTTER_FRAME_HEIGHT"),
                   XA_CARDINAL,
                   32,
                   PropModeReplace,
                   (guchar *) &content->frame_height, 1);
}

static void
meta_frame_content_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *minimum_baseline,
                            int            *natural_baseline)
{
  MetaFrameContent *content = META_FRAME_CONTENT (widget);
  GdkDisplay *display = gtk_widget_get_display (widget);
  XWindowAttributes attrs;

  if (content->width < 0 || content->height < 0)
    {
      XGetWindowAttributes (gdk_x11_display_get_xdisplay (display),
                            content->content, &attrs);
      content->width = attrs.width;
      content->height = attrs.height;
    }

  *minimum_baseline = *natural_baseline = -1;
  *minimum = 1;
  *natural = (orientation == GTK_ORIENTATION_VERTICAL) ?
    content->height : content->width;
}

static void
meta_frame_content_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  MetaFrameContent *content = META_FRAME_CONTENT (widget);
  GtkWindow *window = GTK_WINDOW (gtk_widget_get_root (widget));
  GdkDisplay *display = gtk_widget_get_display (widget);
  double x = 0, y = 0, sx, sy, scale;

  scale = gdk_surface_get_scale_factor (gtk_native_get_surface (GTK_NATIVE (window)));
  gtk_widget_translate_coordinates (widget,
                                    GTK_WIDGET (window),
                                    x, y,
                                    &x, &y);

  meta_frame_content_update_frame_height (content, y);

  gtk_native_get_surface_transform (GTK_NATIVE (window),
                                    &sx, &sy);
  x += sx;
  y += sy;

  if (content->rect.x != x || content->rect.y != y ||
      content->rect.width != width || content->rect.height != height)
    {
      XMoveResizeWindow (gdk_x11_display_get_xdisplay (display),
                         content->content,
                         x * scale,
                         y * scale,
                         width * scale,
                         height * scale);
      content->rect = (cairo_rectangle_int_t) { x, y, width, height };
    }
}

static void
meta_frame_content_class_init (MetaFrameContentClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->measure = meta_frame_content_measure;
  widget_class->size_allocate = meta_frame_content_size_allocate;
}

static void
meta_frame_content_init (MetaFrameContent *content)
{
  content->width = content->height = -1;
  content->frame_height = -1;
}

static GtkWidget *
meta_frame_content_new (Display *xdisplay,
                        Window   window)
{
  MetaFrameContent *content;

  content = g_object_new (META_TYPE_FRAME_CONTENT, NULL);
  content->content = window;

  XSelectInput (xdisplay, window,
                PropertyChangeMask | StructureNotifyMask);

  return GTK_WIDGET (content);
}

static gboolean
on_frame_close_request (GtkWindow *window,
                        gpointer   user_data)
{
  MetaFrameContent *content = user_data;
  XClientMessageEvent ev;
  GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (window));

  ev.type = ClientMessage;
  ev.window = content->content;
  ev.message_type =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_PROTOCOLS");
  ev.format = 32;
  ev.data.l[0] =
    gdk_x11_get_xatom_by_name_for_display (display, "WM_DELETE_WINDOW");
  ev.data.l[1] = 0; /* FIXME: missing timestamp */

  gdk_x11_display_error_trap_push (display);
  XSendEvent (gdk_x11_display_get_xdisplay (display),
              content->content, False, 0, (XEvent*) &ev);
  gdk_x11_display_error_trap_pop_ignored (display);

  return TRUE;
}

static gboolean
on_xevent (GdkDisplay *display,
           XEvent     *xevent,
           gpointer    user_data)
{
  GtkWindow *frame;

  if (xevent->type == ClientMessage &&
      xevent->xclient.message_type ==
      gdk_x11_get_xatom_by_name_for_display (display, "_MUTTER_FRAME"))
    {
      Window client_window = xevent->xclient.data.l[0];

      /* Double check it's not a request for a frame of our own. */
      if (!g_hash_table_contains (frames, GUINT_TO_POINTER (client_window)))
        {
          GtkWidget *header, *content;
          GdkSurface *surface;
          unsigned long data[1];
          Window xframe;

          /* Create a frame window */
          XGrabServer (gdk_x11_display_get_xdisplay (display));

          frame = GTK_WINDOW (gtk_window_new ());

          header = gtk_header_bar_new ();
          gtk_window_set_titlebar (GTK_WINDOW (frame), header);
          content = meta_frame_content_new (gdk_x11_display_get_xdisplay (display),
                                            client_window);
          gtk_window_set_child (frame, content);

          g_signal_connect (frame, "close-request",
                            G_CALLBACK (on_frame_close_request), content);

          gtk_widget_show (GTK_WIDGET (frame));
          surface = gtk_native_get_surface (GTK_NATIVE (frame));
          gdk_x11_surface_set_frame_sync_enabled (surface, FALSE);
          xframe = gdk_x11_surface_get_xid (surface);

          data[0] = client_window;
          XChangeProperty (gdk_x11_display_get_xdisplay (display),
                           xframe,
                           gdk_x11_get_xatom_by_name_for_display (display, "_MUTTER_FRAME_FOR"),
                           XA_WINDOW,
                           32,
                           PropModeReplace,
                           (guchar *) data, 1);

          XUngrabServer (gdk_x11_display_get_xdisplay (display));

          g_hash_table_insert (frames, GUINT_TO_POINTER (xframe), frame);
          g_hash_table_insert (client_windows, GUINT_TO_POINTER (client_window), frame);
        }
    }
  else if (xevent->type == DestroyNotify)
    {
      frame = g_hash_table_lookup (client_windows,
                                   GUINT_TO_POINTER (xevent->xdestroywindow.window));

      if (frame)
        {
          GdkSurface *surface;
          Window xframe;

          surface = gtk_native_get_surface (GTK_NATIVE (frame));
          xframe = gdk_x11_surface_get_xid (surface);

          g_hash_table_remove (client_windows,
                               GUINT_TO_POINTER (xevent->xdestroywindow.window));
          g_hash_table_remove (frames, GUINT_TO_POINTER (xframe));
        }
    }
  else if (xevent->type == PropertyNotify &&
           xevent->xproperty.atom ==
           gdk_x11_get_xatom_by_name_for_display (display, "_NET_WM_NAME"))
    {
      frame = g_hash_table_lookup (client_windows,
                                   GUINT_TO_POINTER (xevent->xproperty.window));
      if (frame)
        {
          char *title = NULL;

          if (xevent->xproperty.state == PropertyNewValue)
            {
              int format;
              Atom type;
              unsigned long nitems, bytes_after;

              XGetWindowProperty (gdk_x11_display_get_xdisplay (display),
                                  xevent->xproperty.window,
                                  xevent->xproperty.atom,
                                  0, G_MAXLONG, False,
                                  gdk_x11_get_xatom_by_name_for_display (display,
                                                                         "UTF8_STRING"),
                                  &type, &format,
                                  &nitems, &bytes_after,
                                  (unsigned char **) &title);
            }

          gtk_window_set_title (frame, title);
          g_free (title);
        }
    }
  else if (xevent->type == PropertyNotify &&
           xevent->xproperty.atom ==
           gdk_x11_get_xatom_by_name_for_display (display, "_MOTIF_WM_HINTS"))
    {
      frame = g_hash_table_lookup (client_windows,
                                   GUINT_TO_POINTER (xevent->xproperty.window));
      if (frame)
        {
          MotifWmHints *mwm_hints = NULL;

          if (xevent->xproperty.state == PropertyNewValue)
            {
              int format;
              Atom type;
              unsigned long nitems, bytes_after;

              XGetWindowProperty (gdk_x11_display_get_xdisplay (display),
                                  xevent->xproperty.window,
                                  xevent->xproperty.atom,
                                  0, sizeof (MotifWmHints) / sizeof (long),
                                  False, AnyPropertyType,
                                  &type, &format,
                                  &nitems, &bytes_after,
                                  (unsigned char **) &mwm_hints);
            }

          gtk_window_set_deletable (frame, (mwm_hints->functions & MWM_FUNC_CLOSE) == 0);
          g_free (mwm_hints);
        }
    }
  else if (xevent->type == ConfigureNotify)
    {
      frame = g_hash_table_lookup (client_windows,
                                   GUINT_TO_POINTER (xevent->xconfigure.window));
      if (frame)
        {
          MetaFrameContent *content;

          content = META_FRAME_CONTENT (gtk_window_get_child (frame));

          if (xevent->xconfigure.width !=
              gtk_widget_get_allocated_width (GTK_WIDGET (content)) ||
              xevent->xconfigure.height !=
              gtk_widget_get_allocated_height (GTK_WIDGET (content)))
            {
              content->width = xevent->xconfigure.width;
              content->height = xevent->xconfigure.height;
              gtk_widget_queue_resize (GTK_WIDGET (content));
            }
        }
    }

  return GDK_EVENT_PROPAGATE;
}

int
main (int   argc,
      char *argv[])
{
  GdkDisplay *display;
  GMainLoop *loop;
  Display *xdisplay;
  Window xroot;

  /* This seems to be the renderer that works best with
   * frame sync disabled.
   */
  g_setenv ("GSK_RENDERER", "cairo", TRUE);

  gdk_set_allowed_backends ("x11");

  gtk_init ();

  frames = g_hash_table_new_full (NULL, NULL, NULL,
                                  (GDestroyNotify) gtk_window_destroy);
  client_windows = g_hash_table_new (NULL, NULL);

  display = gdk_display_get_default ();

  xdisplay = gdk_x11_display_get_xdisplay (display);
  xroot = gdk_x11_display_get_xrootwindow (display);
  XSelectInput (xdisplay, xroot,
                KeyPressMask |
                PropertyChangeMask);

  g_signal_connect (display, "xevent",
                    G_CALLBACK (on_xevent), NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  return 0;
}
