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
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include <glib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video-info.h>

#define V4L2_EVENT_PRI_CLIENT_USAGE  V4L2_EVENT_PRIVATE_START

struct v4l2_event_client_usage {
  __u32 count;
};

GST_DEBUG_CATEGORY_STATIC (gst_debug_category);
#define GST_CAT_DEFAULT gst_debug_category

static gboolean opt_background = FALSE;
static gboolean opt_debug = FALSE;
static gboolean opt_version = FALSE;
static gchar *opt_input = NULL;
static gchar *opt_output = NULL;

static GMainLoop *loop = NULL;
static guint input_bus_watch_id = 0;
static guint output_bus_watch_id = 0;
static guint output_push_buffer_id = 0;
static guint v4l2_event_poll_id = 0;
static GstElement *input_pipeline = NULL;
static GstElement *output_pipeline = NULL;

static gboolean    input_pipeline_bus_call (GstBus     *bus,
                                            GstMessage *msg,
                                            gpointer    data);
static GstElement* input_pipeline_create   (GstElement *appsrc);

static const GOptionEntry opt_entries[] =
{
  { "background", 'D', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_background, "Run in the background", NULL },
  { "debug",      'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_debug, "Print debugging information", NULL },
  { "version",    'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_version, "Show version", NULL },
  { "input",      'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
    &opt_input, "Specify input GStreamer pipeline description", NULL},
  { "output",     'o', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME,
    &opt_output, "Specify output GStreamer pipeline description", NULL},
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

static gboolean
input_pipeline_bus_call (GstBus     *bus,
                         GstMessage *msg,
                         gpointer    data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      GST_ERROR ("%s", error->message);
      g_error_free (error);

      gst_element_set_state (input_pipeline, GST_STATE_NULL);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static GstFlowReturn
input_appsink_new_sample (GstAppSink *appsink,
                          gpointer    user_data)
{
  GstAppSrc *appsrc = (GstAppSrc *) user_data;
  GstSample *sample;
  GstBuffer *buffer;

  sample = gst_app_sink_pull_sample (appsink);
  buffer = gst_sample_get_buffer (sample);
  /* gst_app_src_push_buffer wants to take the ownership of the buffer,
   * so it must hold an additional reference first. */
  gst_buffer_ref (buffer);
  gst_app_src_push_buffer (appsrc, buffer);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static GstElement*
input_pipeline_create (GstElement *appsrc)
{
  GstElement *pipeline, *appsink;
  GstClock *clock;
  GError *error = NULL;
  GstCaps *caps;
  GstBus *bus;

  pipeline = gst_parse_launch (opt_input, &error);
  if (pipeline == NULL) {
    GST_ERROR ("%s", error->message);
    g_error_free (error);
    return NULL;
  }
  gst_object_ref_sink (pipeline);

  clock = gst_system_clock_obtain ();
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), clock);
  gst_element_set_base_time (pipeline,
                             gst_element_get_base_time (output_pipeline));
  gst_element_set_start_time (pipeline, GST_CLOCK_TIME_NONE);
  gst_object_unref (clock);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "appsink");
  caps = gst_app_src_get_caps (GST_APP_SRC (appsrc));
  g_object_set (appsink,
                "caps", caps,
                "drop", TRUE,
                "max-buffers", 4,
                "emit-signals", TRUE,
                NULL);
  g_signal_connect_object (appsink,
                           "new-sample", (GCallback) input_appsink_new_sample,
                           appsrc, 0);
  gst_caps_unref (caps);
  gst_object_unref (appsink);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  input_bus_watch_id =
      gst_bus_add_watch (bus, input_pipeline_bus_call, NULL);
  gst_object_unref (bus);

  return pipeline;
}

static void
input_pipeline_enable ()
{
  if (input_pipeline == NULL) {
    GstElement *appsrc;

    appsrc = gst_bin_get_by_name (GST_BIN (output_pipeline), "appsrc");
    input_pipeline = input_pipeline_create (appsrc);
    gst_object_unref (appsrc);
  }

  gst_element_set_state (input_pipeline, GST_STATE_PLAYING);
}

static void
input_pipeline_disable ()
{
  if (input_pipeline != NULL)
    gst_element_set_state (input_pipeline, GST_STATE_NULL);
}

static gboolean
v4l2sink_event_callback (gint         fd,
                         GIOCondition condition,
                         gpointer     user_data G_GNUC_UNUSED)
{
  struct v4l2_event event;
  int ret;

  if (!(condition & G_IO_PRI))
    return TRUE;

  do {
    memset (&event, 0, sizeof (event));

    ret = ioctl (fd, VIDIOC_DQEVENT, &event);
    if (ret < 0)
      return TRUE;

    GST_TRACE ("Received V4L2 event type %u", event.type);
    switch (event.type) {
      case V4L2_EVENT_PRI_CLIENT_USAGE: {
        struct v4l2_event_client_usage usage;

        memcpy (&usage, &event.u, sizeof usage);
        GST_DEBUG ("Current V4L2 client: %u", usage.count);
        if (usage.count)
          input_pipeline_enable ();
        else
          input_pipeline_disable ();

        break;
      }
      default:
        break;
    }
  } while (event.pending);

  return TRUE;
}

static gboolean
output_pipeline_bus_call (GstBus     *bus,
                          GstMessage *msg,
                          gpointer    data G_GNUC_UNUSED)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state;
      GstElement *v4l2sink;
      int fd = -1;
      struct v4l2_event_subscription sub;

      if (!GST_IS_PIPELINE (GST_MESSAGE_SRC (msg)))
        break;

      gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
      GST_DEBUG ("Output pipeline state changed from %s to %s",
                 gst_element_state_get_name (old_state),
                 gst_element_state_get_name (new_state));
      if (old_state == GST_STATE_PLAYING) {
        if (v4l2_event_poll_id > 0) {
          g_source_remove (v4l2_event_poll_id);
          v4l2_event_poll_id = 0;
        }
        break;
      }

      if (new_state != GST_STATE_PLAYING)
        break;

      g_assert (GST_ELEMENT (GST_MESSAGE_SRC (msg)) == output_pipeline);
      v4l2sink = gst_bin_get_by_name (GST_BIN (output_pipeline), "v4l2sink");
      if (v4l2sink == NULL)
        break;

      g_object_get (v4l2sink, "device-fd", &fd, NULL);

      memset (&sub, 0, sizeof (sub));
      sub.type = V4L2_EVENT_PRI_CLIENT_USAGE;
      sub.id = 0;
      sub.flags = V4L2_EVENT_SUB_FL_SEND_INITIAL;
      if (ioctl (fd, VIDIOC_SUBSCRIBE_EVENT, &sub) == 0)
        v4l2_event_poll_id =
            g_unix_fd_add (fd, G_IO_PRI, v4l2sink_event_callback, NULL);
      else
        GST_WARNING ("V4L2_EVENT_PRI_CLIENT_USAGE not supported");

      gst_object_unref (v4l2sink);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      GST_ERROR ("%s", error->message);
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
output_appsrc_push_data (GstAppSrc *appsrc)
{
  GstCaps *caps;
  GstVideoInfo info;
  GstBuffer *buffer;
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
    output_push_buffer_id = 0;

  return ret;
}

static void
output_appsrc_need_data (GstAppSrc *appsrc,
                         guint      length G_GNUC_UNUSED,
                         gpointer   user_data G_GNUC_UNUSED)
{
  GstState state;

  if (output_push_buffer_id != 0)
    return;

  gst_element_get_state (output_pipeline, &state, NULL, 0);
  if (state == GST_STATE_PLAYING)
      return;

  output_push_buffer_id =
      g_idle_add ((GSourceFunc) output_appsrc_push_data, appsrc);
}

static void
output_appsrc_enough_data (GstAppSrc *appsrc G_GNUC_UNUSED,
                           gpointer   user_data G_GNUC_UNUSED)
{
  if (output_push_buffer_id != 0) {
    g_source_remove (output_push_buffer_id);
    output_push_buffer_id = 0;
  }
}

static GstAppSrcCallbacks output_appsrc_callbacks = {
  .need_data = output_appsrc_need_data,
  .enough_data = output_appsrc_enough_data,
  .seek_data = NULL
};

static GstElement*
output_pipeline_create ()
{
  GstElement *pipeline, *appsrc;
  GstClock *clock;
  GError *error = NULL;
  GstBus *bus;

  pipeline = gst_parse_launch (opt_output, &error);
  if (pipeline == NULL) {
    GST_ERROR ("%s", error->message);
    g_error_free (error);
    return NULL;
  }
  gst_object_ref_sink (pipeline);

  clock = gst_system_clock_obtain ();
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), clock);
  gst_element_set_base_time (pipeline, gst_clock_get_time (clock));
  gst_element_set_start_time (pipeline, GST_CLOCK_TIME_NONE);
  gst_object_unref (clock);

  appsrc = gst_bin_get_by_name (GST_BIN (pipeline), "appsrc");
  g_object_set (appsrc,
                "stream-type", GST_APP_STREAM_TYPE_STREAM,
                "format", GST_FORMAT_DEFAULT,
                "is-live", TRUE,
                NULL);
  gst_app_src_set_callbacks (GST_APP_SRC (appsrc), &output_appsrc_callbacks,
                             NULL, NULL);
  gst_object_unref (appsrc);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  output_bus_watch_id =
      gst_bus_add_watch (bus, output_pipeline_bus_call, NULL);
  gst_object_unref (bus);

  return pipeline;
}

int
main (int   argc,
      char *argv[])
{

  parse_args (argc, argv);

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "V4L2_RELAYD", 0, "v4l2-relayd");

  loop = g_main_loop_new (NULL, FALSE);
  output_pipeline = output_pipeline_create ();
  gst_element_set_state (output_pipeline, GST_STATE_PLAYING);

  GST_INFO ("Running...");
  g_main_loop_run (loop);

  g_source_remove (input_bus_watch_id);
  g_source_remove (output_bus_watch_id);

  gst_element_set_state (output_pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (output_pipeline));

  gst_element_set_state (input_pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (input_pipeline));

  g_main_loop_unref (loop);

  return 0;
}
