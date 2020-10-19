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

static gboolean opt_background = FALSE;
static gboolean opt_debug = FALSE;
static gboolean opt_version = FALSE;
static gchar *opt_capture = NULL;
static gchar *opt_output = NULL;

static const GOptionEntry opt_entries[] =
{
  { "background", 'D', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_background, "Run in the background", NULL },
  { "debug",      'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_debug, "Print debugging information", NULL },
  { "version",    'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    &opt_version, "Show version", NULL },
  { "capture",    'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME,
    &opt_capture, "Specify capturing device", NULL},
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

int
main (int   argc,
      char *argv[])
{
  parse_args (argc, argv);

  return 0;
}
