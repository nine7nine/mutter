/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015 Red Hat, Inc.
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

#include "tests/unit-tests.h"

#include <glib.h>
#include <stdlib.h>

#include <meta/main.h>
#include <meta/util.h>

#include "core/boxes-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-context.h"
#include "tests/boxes-tests.h"
#include "tests/monitor-config-migration-unit-tests.h"
#include "tests/monitor-unit-tests.h"
#include "tests/monitor-store-unit-tests.h"
#include "tests/monitor-transform-tests.h"
#include "tests/meta-test-utils.h"
#include "tests/orientation-manager-unit-tests.h"

MetaContext *test_context;

typedef struct _MetaTestLaterOrderCallbackData
{
  GMainLoop *loop; /* Loop to terminate when done. */
  int callback_num; /* Callback number integer. */
  int *expected_callback_num; /* Pointer to the expected callback number. */
} MetaTestLaterOrderCallbackData;

static gboolean
test_later_order_callback (gpointer user_data)
{
  MetaTestLaterOrderCallbackData *data = user_data;

  g_assert_cmpint (data->callback_num, ==, *data->expected_callback_num);

  if (*data->expected_callback_num == 0)
    g_main_loop_quit (data->loop);
  else
    (*data->expected_callback_num)--;

  return FALSE;
}

static void
meta_test_util_later_order (void)
{
  GMainLoop *loop;
  int expected_callback_num;
  int i;
  const int num_callbacks = 3;
  MetaTestLaterOrderCallbackData callback_data[num_callbacks];

  loop = g_main_loop_new (NULL, FALSE);

  /* Schedule three BEFORE_DRAW callbacks each with its own number associated
   * with it.
   */
  for (i = 0; i < num_callbacks; i++)
    {
      callback_data[i] = (MetaTestLaterOrderCallbackData) {
        .loop = loop,
        .callback_num = i,
        .expected_callback_num = &expected_callback_num,
      };
      meta_later_add (META_LATER_BEFORE_REDRAW,
                      test_later_order_callback,
                      &callback_data[i],
                      NULL);
    }

  /* Check that the callbacks are invoked in the opposite order that they were
   * scheduled. Each callback will decrease the number by 1 after it checks the
   * validity.
   */
  expected_callback_num = num_callbacks - 1;
  g_main_loop_run (loop);
  g_assert_cmpint (expected_callback_num, ==, 0);
  g_main_loop_unref (loop);
}

typedef enum _MetaTestLaterScheduleFromLaterState
{
  META_TEST_LATER_EXPECT_CALC_SHOWING,
  META_TEST_LATER_EXPECT_SYNC_STACK,
  META_TEST_LATER_EXPECT_BEFORE_REDRAW,
  META_TEST_LATER_FINISHED,
} MetaTestLaterScheduleFromLaterState;

typedef struct _MetaTestLaterScheduleFromLaterData
{
  GMainLoop *loop;
  MetaTestLaterScheduleFromLaterState state;
} MetaTestLaterScheduleFromLaterData;

static gboolean
test_later_schedule_from_later_sync_stack_callback (gpointer user_data);

static gboolean
test_later_schedule_from_later_calc_showing_callback (gpointer user_data)
{
  MetaTestLaterScheduleFromLaterData *data = user_data;

  g_assert_cmpint (data->state, ==, META_TEST_LATER_EXPECT_CALC_SHOWING);

  meta_later_add (META_LATER_SYNC_STACK,
                  test_later_schedule_from_later_sync_stack_callback,
                  data,
                  NULL);

  data->state = META_TEST_LATER_EXPECT_SYNC_STACK;

  return FALSE;
}

static gboolean
test_later_schedule_from_later_sync_stack_callback (gpointer user_data)
{
  MetaTestLaterScheduleFromLaterData *data = user_data;

  g_assert_cmpint (data->state, ==, META_TEST_LATER_EXPECT_SYNC_STACK);

  data->state = META_TEST_LATER_EXPECT_BEFORE_REDRAW;

  return FALSE;
}

static gboolean
test_later_schedule_from_later_before_redraw_callback (gpointer user_data)
{
  MetaTestLaterScheduleFromLaterData *data = user_data;

  g_assert_cmpint (data->state, ==, META_TEST_LATER_EXPECT_BEFORE_REDRAW);
  data->state = META_TEST_LATER_FINISHED;
  g_main_loop_quit (data->loop);

  return FALSE;
}

static void
meta_test_util_later_schedule_from_later (void)
{
  MetaTestLaterScheduleFromLaterData data;

  data.loop = g_main_loop_new (NULL, FALSE);

  /* Test that scheduling a MetaLater with 'when' being later than the one being
   * invoked causes it to be invoked before any callback with a later 'when'
   * value being invoked.
   *
   * The first and last callback is queued here. The one to be invoked in
   * between is invoked in test_later_schedule_from_later_calc_showing_callback.
   */
  meta_later_add (META_LATER_CALC_SHOWING,
                  test_later_schedule_from_later_calc_showing_callback,
                  &data,
                  NULL);
  meta_later_add (META_LATER_BEFORE_REDRAW,
                  test_later_schedule_from_later_before_redraw_callback,
                  &data,
                  NULL);

  data.state = META_TEST_LATER_EXPECT_CALC_SHOWING;

  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_assert_cmpint (data.state, ==, META_TEST_LATER_FINISHED);
}

static void
meta_test_adjacent_to (void)
{
  MetaRectangle base = { .x = 10, .y = 10, .width = 10, .height = 10 };
  MetaRectangle adjacent[] = {
    { .x = 20, .y = 10, .width = 10, .height = 10 },
    { .x = 0, .y = 10, .width = 10, .height = 10 },
    { .x = 0, .y = 1, .width = 10, .height = 10 },
    { .x = 20, .y = 19, .width = 10, .height = 10 },
    { .x = 10, .y = 20, .width = 10, .height = 10 },
    { .x = 10, .y = 0, .width = 10, .height = 10 },
  };
  MetaRectangle not_adjacent[] = {
    { .x = 0, .y = 0, .width = 10, .height = 10 },
    { .x = 20, .y = 20, .width = 10, .height = 10 },
    { .x = 21, .y = 10, .width = 10, .height = 10 },
    { .x = 10, .y = 21, .width = 10, .height = 10 },
    { .x = 10, .y = 5, .width = 10, .height = 10 },
    { .x = 11, .y = 10, .width = 10, .height = 10 },
    { .x = 19, .y = 10, .width = 10, .height = 10 },
  };
  unsigned int i;

  for (i = 0; i < G_N_ELEMENTS (adjacent); i++)
    g_assert (meta_rectangle_is_adjacent_to (&base, &adjacent[i]));

  for (i = 0; i < G_N_ELEMENTS (not_adjacent); i++)
    g_assert (!meta_rectangle_is_adjacent_to (&base, &not_adjacent[i]));
}

static void
init_tests (void)
{
  g_test_add_func ("/util/meta-later/order", meta_test_util_later_order);
  g_test_add_func ("/util/meta-later/schedule-from-later",
                   meta_test_util_later_schedule_from_later);

  g_test_add_func ("/core/boxes/adjacent-to", meta_test_adjacent_to);

  init_monitor_store_tests ();
  init_monitor_config_migration_tests ();
  init_monitor_tests ();
  init_boxes_tests ();
  init_monitor_transform_tests ();
  init_orientation_manager_tests ();
}

int
main (int argc, char *argv[])
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_NESTED,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  if (!meta_context_configure (context, &argc, &argv, &error))
    g_error ("Failed to configure test context: %s", error->message);

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (pre_run_monitor_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (finish_monitor_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context));
}
