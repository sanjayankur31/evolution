/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-dropdown-menu.c
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors:
 *   Ettore Perazzoli <ettore@ximian.com>
 *   Damon Chaplin <damon@ximian.com> 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome.h>

#include "e-dropdown-button.h"


/* (The following is shameless stolen from `testgnome.c'.  */

static void
item_activated (GtkWidget *widget,
		void *data)
{
	printf ("%s activated.\n", (char *) data);
}

static GnomeUIInfo ui_info[] = {
	{ GNOME_APP_UI_ITEM, "_New", "Create a new file", item_activated, "file/new", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_NEW, 'n', GDK_CONTROL_MASK, NULL },
	{ GNOME_APP_UI_ITEM, "_Open...", "Open an existing file", item_activated, "file/open", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_OPEN, 'o', GDK_CONTROL_MASK, NULL },
	{ GNOME_APP_UI_ITEM, "_Save", "Save the current file", item_activated, "file/save", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_SAVE, 's', GDK_CONTROL_MASK, NULL },
	{ GNOME_APP_UI_ITEM, "Save _as...", "Save the current file with a new name", item_activated, "file/save as", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_SAVE_AS, 0, 0, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, "_Print...", "Print the current file", item_activated, "file/print", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_PRINT, 'p', GDK_CONTROL_MASK, NULL },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, "_Close", "Close the current file", item_activated, "file/close", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_CLOSE, 0, 0, NULL },
	{ GNOME_APP_UI_ITEM, "E_xit", "Exit the program", item_activated, "file/exit", NULL,
	  GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_EXIT, 'q', GDK_CONTROL_MASK, NULL },
	GNOMEUIINFO_END
};


int
main (int argc, char **argv)
{
	GtkWidget *window;
	GtkWidget *menu;
	GtkWidget *dropdown_button;

	gnome_init ("test-dropdown-button", "0.0", argc, argv);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (window), 1, 1);

	menu = gtk_menu_new ();

	gnome_app_fill_menu (GTK_MENU_SHELL (menu), ui_info, NULL, TRUE, 0);

	dropdown_button = e_dropdown_button_new ("Me_nu", GTK_MENU (menu), ui_info, NULL);
	gtk_container_add (GTK_CONTAINER (window), dropdown_button);

	gtk_widget_show (window);
	gtk_widget_show (dropdown_button);

	gtk_main ();

	return 0;
}
