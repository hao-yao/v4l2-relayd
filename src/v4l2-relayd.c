/* v4l2-relayd - V4L2 camera streaming relay daemon
 * Copyright (C) 2020 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 */

#if defined (HAVE_CONFIG_H)
#include "config.h"
#endif

#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <gst/gst.h>

static gboolean opt_background = FALSE;
static gboolean opt_debug = FALSE;
static gboolean opt_version = FALSE;
static gchar *opt_capture = NULL;
static gchar *opt_capture_caps = NULL;
static gchar *opt_output = NULL;

static const GOptionEntry opt_entries[] =
{
  { "background", 'D', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_background, "Run in the background", NULL },
  { "debug",      'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_debug, "Print debugging information", NULL },
  { "version",    'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_version, "Show version", NULL },
  { "capture",    'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
    &opt_capture, "Specify capturing device name", NULL},
  { "capture-caps", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
    &opt_capture_caps, "Specify optional capturing device capabilities", NULL},
  { "output",     'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME,
    &opt_output, "Specify output device", NULL},
  { NULL }
};

static void
parse_args (int   argc,
            char *argv[])
{
  GError *error = NULL;
  GOptionContext *context;

  context = g_option_context_new ("- test tree model performance");
  g_option_context_add_main_entries (context, opt_entries, NULL);
  g_option_context_set_help_enabled (context, TRUE);

  g_option_context_add_group (context, gst_init_get_option_group ());

  if (!g_option_context_parse (context, &argc, &argv, &error))
  {
    if (error != NULL) {
      g_printerr ("option parsing failed: %s\n", error->message);
      g_error_free (error);
    } else
      g_printerr ("option parsing failed: unknown error\n");

    g_option_context_free (context);
    exit (1);
  }

  g_option_context_free (context);

  if (opt_version) {
    g_print ("%s (%s)\n", g_get_prgname (), V4L2_RELAYD_VERSION);
    exit (0);
  }

  if (opt_debug) {
    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,
                       g_log_default_handler, NULL);
    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
                       g_log_default_handler, NULL);
  }

  if (opt_background) {
    if (daemon (0, 0) < 0) {
      int saved_errno;

      saved_errno = errno;
      g_printerr ("Could not daemonize: %s [error %u]\n",
                  g_strerror (saved_errno), saved_errno);
      exit (1);
    }
  }
}

static int
capture_device_id_from_string (const gchar *value)
{
  GType enum_type;
  GEnumClass *enum_class;
  GEnumValue *enum_value;
  int id = -1;

  enum_type = g_type_from_name ("GstCamerasrcDeviceName");
  g_assert (G_TYPE_IS_ENUM (enum_type));
  enum_class = g_type_class_ref (enum_type);
  g_assert (enum_class != NULL);

  enum_value = g_enum_get_value_by_name (enum_class, value);
  if (enum_value == NULL)
    enum_value = g_enum_get_value_by_nick (enum_class, value);
  if (enum_value != NULL)
    id = enum_value->value;

  g_type_class_unref (enum_class);

  return id;
}

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *source, *convert, *sink;
  GstBus *bus;
  guint bus_watch_id;

  parse_args (argc, argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("v4l2-relayd");
  g_object_ref_sink (pipeline);
  source = gst_element_factory_make ("icamerasrc", NULL);
  convert = gst_element_factory_make ("videoconvert", NULL);
  sink = gst_element_factory_make ("xvimagesink", NULL);

  if (opt_capture != NULL) {
    int id;

    id = capture_device_id_from_string (opt_capture);
    if (id >= 0)
      g_object_set (source, "device-name", id, NULL);
  }

  gst_bin_add_many (GST_BIN (pipeline),
                    source, convert, sink, NULL);

  if (opt_capture_caps != NULL) {
    GstCaps *caps;

    caps = gst_caps_from_string (opt_capture_caps);
    gst_element_link_filtered (source, convert, caps);
    gst_element_link (convert, sink);
    gst_caps_unref (caps);
  } else
    gst_element_link_many (source, convert, sink, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  g_print ("Now playing...\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_print ("Running...\n");
  g_main_loop_run (loop);

  g_source_remove (bus_watch_id);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  g_main_loop_unref (loop);

  return 0;
}
