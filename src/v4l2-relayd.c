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
#include <gst/app/gstappsrc.h>
#include <gst/video/video-info.h>

static gboolean opt_background = FALSE;
static gboolean opt_debug = FALSE;
static gboolean opt_version = FALSE;
static gchar *opt_capture = NULL;
static gchar *opt_capture_caps = NULL;
static gchar *opt_output = NULL;
static gchar *opt_output_caps = NULL;

static guint icamerasrc_bus_watch_id = 0;
static guint loopback_bus_watch_id = 0;
static guint loopback_push_buffer_id = 0;

static int         capture_device_id_from_string (const gchar *value) G_GNUC_UNUSED;
static gboolean    icamerasrc_pipeline_bus_call  (GstBus      *bus,
                                                  GstMessage  *msg,
                                                  gpointer     data) G_GNUC_UNUSED;
static GstElement* icamerasrc_pipeline_create    (GMainLoop   *loop) G_GNUC_UNUSED;

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
  { "output-caps", 0,  G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
    &opt_output_caps, "Specify optional output device capabilities", NULL},
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
icamerasrc_pipeline_bus_call (GstBus     *bus,
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

static GstElement*
icamerasrc_pipeline_create (GMainLoop *loop)
{
  GstElement *pipeline, *icamerasrc, *videoconvert, *xvimagesink;
  GstBus *bus;

  pipeline = gst_pipeline_new ("icamerasrc");
  g_object_ref_sink (pipeline);
  icamerasrc = gst_element_factory_make ("icamerasrc", NULL);
  videoconvert = gst_element_factory_make ("videoconvert", NULL);
  xvimagesink = gst_element_factory_make ("xvimagesink", NULL);

  if (opt_capture != NULL) {
    int id;

    id = capture_device_id_from_string (opt_capture);
    if (id >= 0)
      g_object_set (icamerasrc, "device-name", id, NULL);
  }

  gst_bin_add_many (GST_BIN (pipeline),
                    icamerasrc, videoconvert, xvimagesink, NULL);

  if (opt_capture_caps != NULL) {
    GstCaps *caps;

    caps = gst_caps_from_string (opt_capture_caps);
    gst_element_link_filtered (icamerasrc, videoconvert, caps);
    gst_element_link (videoconvert, xvimagesink);
    gst_caps_unref (caps);
  } else
    gst_element_link_many (icamerasrc, videoconvert, xvimagesink, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  icamerasrc_bus_watch_id =
      gst_bus_add_watch (bus, icamerasrc_pipeline_bus_call, loop);
  gst_object_unref (bus);

  return pipeline;
}

static gboolean
loopback_pipeline_bus_call (GstBus     *bus,
                            GstMessage *msg,
                            gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state;

      if (!GST_IS_PIPELINE (GST_MESSAGE_SRC (msg)))
        break;

      gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
      g_print ("Pipeline state changed from %s to %s:\n",
               gst_element_state_get_name (old_state),
               gst_element_state_get_name (new_state));

      break;
    }
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

static gboolean
loopback_appsrc_push_data (GstAppSrc *appsrc)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBuffer *buffer = NULL;
  gboolean ret = G_SOURCE_REMOVE;

  gst_video_info_init (&info);
  caps = gst_app_src_get_caps (appsrc);
  if ((caps == NULL) || !gst_video_info_from_caps (&info, caps))
    goto caps_error;

  buffer = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&info), NULL);
  if (buffer == NULL)
    goto caps_error;

  ret = GST_FLOW_OK == gst_app_src_push_buffer (appsrc, buffer)
      ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;

caps_error:
  if (caps != NULL)
    gst_caps_unref (caps);

  if (ret == G_SOURCE_REMOVE)
    loopback_push_buffer_id = 0;

  return ret;
}

static void
loopback_appsrc_need_data (GstAppSrc *appsrc,
                           guint      length,
                           gpointer   user_data)
{
  if (loopback_push_buffer_id == 0) {
    loopback_push_buffer_id =
        g_idle_add ((GSourceFunc) loopback_appsrc_push_data, appsrc);
  }
}

static void
loopback_appsrc_enough_data (GstAppSrc *appsrc,
                             gpointer   user_data)
{
  if (loopback_push_buffer_id != 0) {
    g_source_remove (loopback_push_buffer_id);
    loopback_push_buffer_id = 0;
  }
}

static GstAppSrcCallbacks loopback_appsrc_callbacks = {
  .need_data = loopback_appsrc_need_data,
  .enough_data = loopback_appsrc_enough_data,
  .seek_data = NULL
};

static GstElement*
loopback_pipeline_create (GMainLoop *loop)
{
  GstElement *pipeline, *appsrc, *videoconvert, *v4l2sink;
  GstBus *bus;

  pipeline = gst_pipeline_new ("v4l2loopback");
  g_object_ref_sink (pipeline);
  appsrc = gst_element_factory_make ("appsrc", NULL);
  videoconvert = gst_element_factory_make ("videoconvert", NULL);
  v4l2sink = gst_element_factory_make ("v4l2sink", NULL);

  g_object_set (appsrc,
                "stream-type", GST_APP_STREAM_TYPE_STREAM,
                "format", GST_FORMAT_DEFAULT,
                "is-live", TRUE,
                NULL);
  if (opt_output_caps != NULL) {
    GstCaps *caps;

    caps = gst_caps_from_string (opt_output_caps);
    g_object_set (appsrc, "caps", caps, NULL);
    gst_caps_unref (caps);
  }

  gst_app_src_set_callbacks (GST_APP_SRC (appsrc), &loopback_appsrc_callbacks,
                             pipeline, NULL);

  g_object_set (v4l2sink, "device", opt_output, NULL);

  gst_bin_add_many (GST_BIN (pipeline), appsrc, videoconvert, v4l2sink, NULL);

  gst_element_link_many (appsrc, videoconvert, v4l2sink, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  loopback_bus_watch_id =
      gst_bus_add_watch (bus, loopback_pipeline_bus_call, loop);
  gst_object_unref (bus);

  return pipeline;
}

int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;
  GstElement *loopback_pipeline;

  parse_args (argc, argv);

  loop = g_main_loop_new (NULL, FALSE);
  loopback_pipeline = loopback_pipeline_create (loop);

  /* Create gstreamer elements */

  g_print ("Now playing...\n");
  gst_element_set_state (loopback_pipeline, GST_STATE_PLAYING);

  g_print ("Running...\n");
  g_main_loop_run (loop);

  g_source_remove (icamerasrc_bus_watch_id);
  g_source_remove (loopback_bus_watch_id);

  gst_element_set_state (loopback_pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (loopback_pipeline));

  g_main_loop_unref (loop);

  return 0;
}
