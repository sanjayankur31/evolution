/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Author: Miguel Angel Lopez Hernandez <miguel@gulev.org.mx>
 *
 *  Copyright 2004 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of version 2 of the GNU General Public
 *  License as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <e-util/e-config.h>
#include <e-util/e-icon-factory.h>

#include <mail/em-utils.h>
#include <mail/em-event.h>
#include <mail/em-folder-tree-model.h>
#include <camel/camel-folder.h>

#ifdef HAVE_LIBNOTIFY	
#include <libnotify/notify.h>
#endif

#define GCONF_KEY_NOTIFICATION "/apps/evolution/mail/notification/notification"
#define GCONF_KEY_BLINK "/apps/evolution/mail/notification/blink-status-icon"

int e_plugin_lib_enable (EPluginLib *ep, int enable);
void org_gnome_mail_new_notify (EPlugin *ep, EMEventTargetFolder *t);
void org_gnome_mail_read_notify (EPlugin *ep, EMEventTargetMessage *t);

static gboolean enabled = FALSE;
GtkStatusIcon *status_icon = NULL;

static GStaticMutex mlock = G_STATIC_MUTEX_INIT;

void
org_gnome_mail_read_notify (EPlugin *ep, EMEventTargetMessage *t)
{
	g_static_mutex_lock (&mlock);

	if (!status_icon) {
		g_static_mutex_unlock (&mlock);
		return;
	}

	gtk_status_icon_set_visible (status_icon, FALSE);

	g_static_mutex_unlock (&mlock);
}

static void
icon_activated (GtkStatusIcon *icon)
{
	g_static_mutex_lock (&mlock);
	gtk_status_icon_set_visible (status_icon, FALSE);
	g_static_mutex_unlock (&mlock);

}

void
org_gnome_mail_new_notify (EPlugin *ep, EMEventTargetFolder *t)
{
	char *msg = NULL;
	char *folder;
	GConfClient *client;
	GConfValue  *is_key;
#ifdef HAVE_LIBNOTIFY	
	NotifyNotification *notify;
#endif
	/* FIXME: Should this is_inbox be configurable? */
	if (!t->new || !t->is_inbox)
		return;

	g_static_mutex_lock (&mlock);
	client = gconf_client_get_default ();

	is_key = gconf_client_get (client, GCONF_KEY_BLINK, NULL);
	if (!is_key)
		gconf_client_set_bool (client, GCONF_KEY_BLINK, TRUE, NULL);

	gconf_value_free (is_key);
	if (!status_icon)
		status_icon = gtk_status_icon_new_from_pixbuf (e_icon_factory_get_icon ("stock_mail", E_ICON_SIZE_LARGE_TOOLBAR));
	
	folder = em_utils_folder_name_from_uri (t->uri);
	msg = g_strdup_printf (_("You have received %d new messages in %s."), t->new, folder);

	gtk_status_icon_set_tooltip (status_icon, msg);
	gtk_status_icon_set_visible (status_icon, TRUE);
	gtk_status_icon_set_blinking (status_icon, 
			gconf_client_get_bool (client, GCONF_KEY_BLINK, NULL));

#ifdef HAVE_LIBNOTIFY	
	/* See whether the notification key has already been set */
	is_key = gconf_client_get (client, GCONF_KEY_NOTIFICATION, NULL);
	if (!is_key)
		gconf_client_set_bool (client, GCONF_KEY_NOTIFICATION, TRUE, NULL);
	gconf_value_free (is_key);

	/* Now check whether we're supposed to send notifications */
	if (gconf_client_get_bool (client, GCONF_KEY_NOTIFICATION, NULL)) {
		if (!notify_init ("evolution-mail-notification"))
			fprintf(stderr,"notify init error");

		notify  = notify_notification_new_with_status_icon (
				"New email", 
				msg, 
				"stock_mail", 
				status_icon);
		notify_notification_set_urgency(notify, NOTIFY_URGENCY_NORMAL);
		notify_notification_set_timeout(notify, NOTIFY_EXPIRES_DEFAULT);
		notify_notification_show(notify, NULL);
	}
#endif

	g_free (folder);
	g_free (msg);
	g_object_unref (client);
	g_signal_connect (G_OBJECT (status_icon), "activate",
			  G_CALLBACK (icon_activated), NULL);
	g_static_mutex_unlock (&mlock);
}




int
e_plugin_lib_enable (EPluginLib *ep, int enable)
{
	if (enable)
		enabled = TRUE;
	else
		enabled = FALSE;
	
	return 0;
}

