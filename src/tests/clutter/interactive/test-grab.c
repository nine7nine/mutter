#include <gmodule.h>
#include <clutter/clutter.h>

#include "tests/clutter-test-utils.h"

int
test_grab_main (int argc, char *argv[]);

const char *
test_grab_describe (void);

static gboolean
debug_event_cb (ClutterActor *actor,
                ClutterEvent *event,
                gpointer      data)
{
  gchar keybuf[9], *source = (gchar*)data;
  int   len = 0;

  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
      len = g_unichar_to_utf8 (clutter_keysym_to_unicode (event->key.keyval),
			       keybuf);
      keybuf[len] = '\0';
      printf ("[%s] KEY PRESS '%s'", source, keybuf);
      break;
    case CLUTTER_KEY_RELEASE:
      len = g_unichar_to_utf8 (clutter_keysym_to_unicode (event->key.keyval),
			       keybuf);
      keybuf[len] = '\0';
      printf ("[%s] KEY RELEASE '%s'", source, keybuf);
      break;
    case CLUTTER_MOTION:
      printf("[%s] MOTION", source);
      break;
    case CLUTTER_ENTER:
      printf("[%s] ENTER", source);
      break;
    case CLUTTER_LEAVE:
      printf("[%s] LEAVE", source);
      break;
    case CLUTTER_BUTTON_PRESS:
      printf("[%s] BUTTON PRESS", source);
      break;
    case CLUTTER_BUTTON_RELEASE:
      printf("[%s] BUTTON RELEASE", source);
      break;
    case CLUTTER_SCROLL:
      printf("[%s] BUTTON SCROLL", source);
      break;
    case CLUTTER_TOUCH_BEGIN:
      g_print ("[%s] TOUCH BEGIN", source);
      break;
    case CLUTTER_TOUCH_UPDATE:
      g_print ("[%s] TOUCH UPDATE", source);
      break;
    case CLUTTER_TOUCH_END:
      g_print ("[%s] TOUCH END", source);
      break;
    case CLUTTER_TOUCH_CANCEL:
      g_print ("[%s] TOUCH CANCEL", source);
      break;
    case CLUTTER_TOUCHPAD_PINCH:
      g_print ("[%s] TOUCHPAD PINCH", source);
      break;
    case CLUTTER_TOUCHPAD_SWIPE:
      g_print ("[%s] TOUCHPAD SWIPE", source);
      break;
    case CLUTTER_TOUCHPAD_HOLD:
      g_print ("[%s] TOUCHPAD HOLD", source);
      break;
    case CLUTTER_PROXIMITY_IN:
      g_print ("[%s] PROXIMITY IN", source);
      break;
    case CLUTTER_PROXIMITY_OUT:
      g_print ("[%s] PROXIMITY OUT", source);
      break;
    case CLUTTER_PAD_BUTTON_PRESS:
      g_print ("[%s] PAD BUTTON PRESS", source);
      break;
    case CLUTTER_PAD_BUTTON_RELEASE:
      g_print ("[%s] PAD BUTTON RELEASE", source);
      break;
    case CLUTTER_PAD_STRIP:
      g_print ("[%s] PAD STRIP", source);
      break;
    case CLUTTER_PAD_RING:
      g_print ("[%s] PAD RING", source);
      break;
    case CLUTTER_NOTHING:
    default:
      return FALSE;
    }

  if (clutter_event_get_source (event) == actor)
    printf(" *source*");
  
  printf("\n");

  return FALSE;
}

static gboolean
grab_pointer_cb (ClutterActor    *actor,
                 ClutterEvent    *event,
                 gpointer         data)
{
  ClutterInputDevice *device = clutter_event_get_device (event);

  clutter_input_device_grab (device, actor);
  return FALSE;
}

static gboolean
red_release_cb (ClutterActor    *actor,
                ClutterEvent    *event,
                gpointer         data)
{
  ClutterInputDevice *device = clutter_event_get_device (event);

  clutter_input_device_ungrab (device);
  return FALSE;
}

static gboolean
blue_release_cb (ClutterActor    *actor,
                 ClutterEvent    *event,
                 gpointer         data)
{
  clutter_actor_destroy (actor);
  return FALSE;
}

static gboolean
green_press_cb (ClutterActor    *actor,
                ClutterEvent    *event,
                gpointer         data)
{
  ClutterActor *stage;
  gboolean enabled;

  stage = clutter_actor_get_stage (actor);
  enabled = !clutter_stage_get_motion_events_enabled (CLUTTER_STAGE (stage));

  clutter_stage_set_motion_events_enabled (CLUTTER_STAGE (stage), enabled);

  g_print ("per actor motion events are now %s\n",
           enabled ? "enabled" : "disabled");

  return FALSE;
}

static gboolean
toggle_grab_pointer_cb (ClutterActor    *actor,
                        ClutterEvent    *event,
                        gpointer         data)
{
  ClutterInputDevice *device = clutter_event_get_device (event);

  /* we only deal with the event if the source is ourself */
  if (event->button.source == actor)
    {
      if (clutter_input_device_get_grabbed_actor (device) != NULL)
        clutter_input_device_ungrab (device);
      else
        clutter_input_device_grab (device, actor);
    }

  return FALSE;
}

static gboolean
cyan_press_cb (ClutterActor    *actor,
               ClutterEvent    *event,
               gpointer         data)
{
  ClutterBackend *backend = clutter_get_default_backend ();
  ClutterSeat *seat = clutter_backend_get_default_seat (backend);
  ClutterInputDevice *device = clutter_seat_get_pointer (seat);

  if (clutter_input_device_get_grabbed_actor (device) != NULL)
    clutter_input_device_ungrab (device);
  else
    clutter_input_device_grab (device, actor);

  return FALSE;
}



G_MODULE_EXPORT int
test_grab_main (int argc, char *argv[])
{
  ClutterActor   *stage, *actor;
  ClutterColor    rcol = { 0xff, 0, 0, 0xff}, 
                  bcol = { 0, 0, 0xff, 0xff },
		  gcol = { 0, 0xff, 0, 0xff },
		  ccol = { 0, 0xff, 0xff, 0xff },
		  ycol = { 0xff, 0xff, 0, 0xff };

  clutter_test_init (&argc, &argv);

  g_print ("Red box:    acquire grab on press, releases it on next button release\n");
  g_print ("Blue box:   acquire grab on press, destroys the blue box actor on release\n");
  g_print ("Yellow box: acquire grab on press, releases grab on next press on yellow box\n");
  g_print ("Green box:  toggle per actor motion events.\n\n");
  g_print ("Cyan  box:  toggle grab (from cyan box) for keyboard events.\n\n");

  stage = clutter_test_get_stage ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Grabs");
  g_signal_connect (stage, "destroy", G_CALLBACK (clutter_test_quit), NULL);
  g_signal_connect (stage, "event",
                    G_CALLBACK (debug_event_cb), (char *) "stage");

  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, &rcol);
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 100, 100);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_container_add (CLUTTER_CONTAINER (stage), actor, NULL);
  g_signal_connect (actor, "event", G_CALLBACK (debug_event_cb), (char *) "red box");
  g_signal_connect (actor, "button-press-event",
                    G_CALLBACK (grab_pointer_cb), NULL);
  g_signal_connect (actor, "button-release-event",
                    G_CALLBACK (red_release_cb), NULL);

  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, &ycol);
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 100, 300);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_container_add (CLUTTER_CONTAINER (stage), actor, NULL);
  g_signal_connect (actor, "event", G_CALLBACK (debug_event_cb), (char *) "yellow box");
  g_signal_connect (actor, "button-press-event",
                    G_CALLBACK (toggle_grab_pointer_cb), NULL);

  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, &bcol);
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 300, 100);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_container_add (CLUTTER_CONTAINER (stage), actor, NULL);
  g_signal_connect (actor, "event",
                    G_CALLBACK (debug_event_cb), (char *) "blue box");
  g_signal_connect (actor, "button-press-event",
                    G_CALLBACK (grab_pointer_cb), NULL);
  g_signal_connect (actor, "button-release-event",
                    G_CALLBACK (blue_release_cb), NULL);

  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, &gcol);
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 300, 300);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_container_add (CLUTTER_CONTAINER (stage), actor, NULL);
  g_signal_connect (actor, "event",
                    G_CALLBACK (debug_event_cb), (char *) "green box");
  g_signal_connect (actor, "button-press-event",
                    G_CALLBACK (green_press_cb), NULL);


  actor = clutter_actor_new ();
  clutter_actor_set_background_color (actor, &ccol);
  clutter_actor_set_size (actor, 100, 100);
  clutter_actor_set_position (actor, 500, 100);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_container_add (CLUTTER_CONTAINER (stage), actor, NULL);
  g_signal_connect (actor, "event",
                    G_CALLBACK (debug_event_cb), (char *) "cyan box");
  g_signal_connect (actor, "button-press-event",
                    G_CALLBACK (cyan_press_cb), NULL);

  clutter_actor_show (CLUTTER_ACTOR (stage));

  clutter_test_main ();

  return 0;
}

G_MODULE_EXPORT const char *
test_grab_describe (void)
{
  return "Examples of using actor grabs";
}
